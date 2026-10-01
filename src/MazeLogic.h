#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace maze {

constexpr float pi = 3.14159265358979323846f;
constexpr float roomHeight = .82f;
enum Direction : int { north = 0, east = 1, south = 2, west = 3 };
int dx(int direction);
int dz(int direction);
int opposite(int direction);

struct Point { int x = 0, z = 0; bool operator==(const Point&) const = default; };
enum class WallPicture : uint8_t { none, teapot, dx, yb };
struct Cell {
    std::array<bool, 4> open{};
    std::array<WallPicture, 4> picture{};
};
struct Polyhedron { Point cell; int type = 0; bool active = true; };

class Grid {
public:
    int size = 16;
    std::vector<Cell> cells;
    Point start{}, startSign{}, finish{}, ratStart{};
    int startDirection = east;
    std::vector<Polyhedron> polyhedra;
    std::vector<Point> glSigns;

    bool inside(int x, int z) const;
    const Cell& at(int x, int z) const;
    Cell& at(int x, int z);
    int openCount(Point point) const;
    void generate(int dimension, std::mt19937& random);
};

class Walker {
public:
    int x = 0, z = 0, direction = east;
    float px = .5f, pz = .5f, yaw = 0.f;
    bool moving = false, turning = false;
    void reset(Point cell, int facing);
    // Returns true when the walker arrived at a cell center.
    bool update(const Grid& grid, float seconds, float cellsPerSecond, float turnsPerSecond);
private:
    int targetX_ = 0, targetZ_ = 0;
    float progress_ = 0.f, turnFrom_ = 0.f, turnTo_ = 0.f;
    bool justTurned_ = false;
    void choose(const Grid& grid);
};

class World {
public:
    Grid grid;
    Walker camera, rat;
    float wallHeight = 0.f; // 0 = underground; 1 = fully raised
    float roll = 0.f;       // zero or pi after a polyhedron collision
    float elapsed = 0.f;
    bool regenerating = false;
    explicit World(uint32_t seed = std::random_device{}());
    void reset(int dimension);
    // True when the topology changed and the GPU geometry must be rebuilt.
    bool update(float seconds, int speed);
private:
    enum class Phase { rising, exploring, rolling, sinking };
    std::mt19937 random_;
    Phase phase_ = Phase::rising;
    float rollFrom_ = 0.f, rollProgress_ = 0.f;
    int dimension_ = 16;
};

} // namespace maze
