#include "MazeLogic.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace maze {

int dx(int direction) { return direction == east ? 1 : direction == west ? -1 : 0; }
int dz(int direction) { return direction == south ? 1 : direction == north ? -1 : 0; }
int opposite(int direction) { return (direction + 2) & 3; }

bool Grid::inside(int x, int z) const { return x >= 0 && z >= 0 && x < size && z < size; }
const Cell& Grid::at(int x, int z) const { return cells.at(z * size + x); }
Cell& Grid::at(int x, int z) { return cells.at(z * size + x); }
int Grid::openCount(Point point) const {
    return int(std::count(at(point.x, point.z).open.begin(), at(point.x, point.z).open.end(), true));
}

void Grid::generate(int dimension, std::mt19937& random) {
    size = std::clamp(dimension, 8, 32);
    cells.assign(size * size, {});
    polyhedra.clear(); glSigns.clear();

    // maze.js newMaze(): randomized iterative depth first backtracker.
    std::uniform_int_distribution<int> cellPicker(0, size - 1);
    Point current{cellPicker(random), cellPicker(random)};
    std::vector<Point> path{current};
    std::vector<int> incomingDirections{-1};
    std::vector<bool> visited(size * size, false);
    visited[current.z * size + current.x] = true;
    int count = 1;
    while (count < size * size) {
        std::array<int, 4> candidate{};
        int available = 0;
        for (int d = 0; d < 4; ++d) {
            int nx = current.x + dx(d), nz = current.z + dz(d);
            if (inside(nx, nz) && !visited[nz * size + nx]) candidate[available++] = d;
        }
        if (available) {
            int direction = candidate[std::uniform_int_distribution<int>(0, available - 1)(random)];
            // Favor long corridors while retaining occasional branches and
            // the original connected, acyclic depth-first topology.
            int incoming = incomingDirections.back();
            if (incoming >= 0 &&
                std::find(candidate.begin(), candidate.begin() + available, incoming) != candidate.begin() + available &&
                std::uniform_int_distribution<int>(0, 99)(random) < 78) {
                direction = incoming;
            }
            Point next{current.x + dx(direction), current.z + dz(direction)};
            at(current.x, current.z).open[direction] = true;
            at(next.x, next.z).open[opposite(direction)] = true;
            visited[next.z * size + next.x] = true;
            ++count;
            current = next;
            path.push_back(current);
            incomingDirections.push_back(direction);
        } else {
            path.pop_back();
            incomingDirections.pop_back();
            if (path.empty()) throw std::logic_error("Backtracker exhausted before visiting all cells");
            current = path.back();
        }
    }

    // maze.js resetVars(): reserve random distinct cells for the start, the
    // sign in front of it, six polyhedra, three OpenGL signs, finish, and rat.
    std::vector<Point> places;
    places.reserve(size * size);
    for (int z = 0; z < size; ++z) for (int x = 0; x < size; ++x) places.push_back({x, z});
    auto take = [&]() {
        auto index = std::uniform_int_distribution<size_t>(0, places.size() - 1)(random);
        Point selected = places[index];
        places[index] = places.back();
        places.pop_back();
        return selected;
    };
    start = take();
    // The JS viewer starts east when possible, then north/south/west.
    for (int direction : {east, north, south, west}) {
        if (at(start.x, start.z).open[direction]) {
            startDirection = direction;
            break;
        }
    }
    startSign = {start.x + dx(startDirection), start.z + dz(startDirection)};
    auto sign = std::find(places.begin(), places.end(), startSign);
    if (sign != places.end()) places.erase(sign);
    for (int i = 0; i < std::min(6, int(places.size()) - 5); ++i)
        polyhedra.push_back({take(), std::uniform_int_distribution<int>(0, 3)(random), true});
    for (int i = 0; i < std::min(3, int(places.size()) - 2); ++i) glSigns.push_back(take());
    finish = take();
    ratStart = take();

    std::vector<std::pair<Point, int>> eligible;
    for (int z = 0; z < size; ++z) for (int x = 0; x < size; ++x)
        for (int d = 0; d < 4; ++d) if (!at(x, z).open[d]) eligible.push_back({{x, z}, d});
    auto placePicture = [&](WallPicture picture) {
        if (eligible.empty()) return;
        auto index = std::uniform_int_distribution<size_t>(0, eligible.size() - 1)(random);
        auto [point, direction] = eligible[index];
        at(point.x, point.z).picture[direction] = picture;
        Point oppositeCell{point.x + dx(direction), point.z + dz(direction)};
        std::erase_if(eligible, [&](const auto& side) {
            return (side.first == point && side.second == direction) ||
                   (side.first == oppositeCell && side.second == opposite(direction));
        });
    };
    for (int i = 0; i < 3; ++i) placePicture(WallPicture::teapot);
    if (std::bernoulli_distribution(.5)(random)) placePicture(WallPicture::dx);
    if (std::bernoulli_distribution(.5)(random)) placePicture(WallPicture::yb);
}

void Walker::reset(Point cell, int facing) {
    x = cell.x; z = cell.z; direction = facing;
    px = float(x) + .5f; pz = float(z) + .5f;
    yaw = (direction - east) * (pi * .5f);
    moving = turning = justTurned_ = false;
    progress_ = 0.f;
}

void Walker::choose(const Grid& grid) {
    const auto& walls = grid.at(x, z).open;
    if (justTurned_ && walls[direction]) {
        justTurned_ = false;
    } else if (walls[(direction + 1) & 3]) {
        turnFrom_ = yaw; turnTo_ = yaw + pi * .5f;
        direction = (direction + 1) & 3;
        progress_ = 0.f; turning = true; justTurned_ = true; return;
    } else if (walls[direction]) {
        justTurned_ = false;
    } else if (walls[(direction + 3) & 3]) {
        turnFrom_ = yaw; turnTo_ = yaw - pi * .5f;
        direction = (direction + 3) & 3;
        progress_ = 0.f; turning = true; justTurned_ = true; return;
    } else {
        turnFrom_ = yaw; turnTo_ = yaw + pi * .5f;
        direction = (direction + 1) & 3;
        progress_ = 0.f; turning = true; justTurned_ = true; return;
    }
    targetX_ = x + dx(direction); targetZ_ = z + dz(direction);
    if (!grid.inside(targetX_, targetZ_)) throw std::logic_error("Navigator crossed maze boundary");
    progress_ = 0.f; moving = true;
}

bool Walker::update(const Grid& grid, float seconds, float cellsPerSecond, float turnsPerSecond) {
    float remaining = std::clamp(seconds, 0.f, .1f);
    while (remaining > .000001f) {
        if (!turning && !moving) choose(grid);
        if (turning) {
            float step = std::min(remaining, (1.f - progress_) / turnsPerSecond);
            progress_ = std::min(1.f, progress_ + step * turnsPerSecond);
            remaining -= step;
            float t = progress_ * progress_ * (3.f - 2.f * progress_);
            yaw = std::lerp(turnFrom_, turnTo_, t);
            if (progress_ >= 1.f - .00001f) { yaw = turnTo_; turning = false; }
        } else {
            float step = std::min(remaining, (1.f - progress_) / cellsPerSecond);
            progress_ = std::min(1.f, progress_ + step * cellsPerSecond);
            remaining -= step;
            px = std::lerp(float(x) + .5f, float(targetX_) + .5f, progress_);
            pz = std::lerp(float(z) + .5f, float(targetZ_) + .5f, progress_);
            if (progress_ >= 1.f - .00001f) {
                x = targetX_; z = targetZ_; moving = false;
                return true;
            }
        }
    }
    return false;
}

World::World(uint32_t seed) : random_(seed) {}

void World::reset(int dimension) {
    dimension_ = std::clamp(dimension, 8, 32);
    grid.generate(dimension_, random_);
    camera.reset(grid.start, grid.startDirection);
    rat.reset(grid.ratStart, std::uniform_int_distribution<int>(0, 3)(random_));
    wallHeight = 0.f; roll = 0.f; elapsed = 0.f;
    phase_ = Phase::rising;
    regenerating = false;
}

bool World::update(float seconds, int speed) {
    float dt = std::clamp(seconds, 0.f, .05f);
    elapsed += dt;
    const float pace = .5f * float(std::clamp(speed, 1, 10));
    // maze.js advances the camera twice per frame and the rat once. Rat turns
    // are consumed immediately in its while(ratdtheta) loop.
    rat.update(grid, dt, pace * .5f, 1000.f);
    switch (phase_) {
    case Phase::rising:
        wallHeight = std::min(1.f, wallHeight + dt * 1.2f);
        if (wallHeight >= 1.f) phase_ = Phase::exploring;
        break;
    case Phase::exploring:
        if (camera.update(grid, dt, pace, pace)) {
            if (Point{camera.x, camera.z} == grid.finish) {
                phase_ = Phase::sinking;
            } else {
                for (auto& poly : grid.polyhedra) {
                    if (poly.active && poly.cell == Point{camera.x, camera.z}) {
                        poly.active = false;
                        rollFrom_ = roll;
                        rollProgress_ = 0.f;
                        phase_ = Phase::rolling;
                        break;
                    }
                }
            }
        }
        break;
    case Phase::rolling: {
        rollProgress_ = std::min(1.f, rollProgress_ + dt / 1.5f);
        float t = rollProgress_ * rollProgress_ * (3.f - 2.f * rollProgress_);
        roll = std::lerp(rollFrom_, rollFrom_ + pi, t);
        if (rollProgress_ >= 1.f) phase_ = Phase::exploring;
        break;
    }
    case Phase::sinking:
        wallHeight = std::max(0.f, wallHeight - dt * 1.2f);
        if (wallHeight <= 0.f) { reset(dimension_); return true; }
        break;
    }
    return false;
}

} // namespace maze
