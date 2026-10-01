#include "MazeLogic.h"
#include <cmath>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <vector>

void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int verifyTopology(const maze::Grid& grid) {
    const int n=grid.size;
    std::vector<bool> seen(n*n,false);
    std::queue<maze::Point> queue;
    queue.push(grid.start); seen[grid.start.z*n+grid.start.x]=true;
    int edges=0, teapots=0, dxPictures=0, ybPictures=0;
    while (!queue.empty()) {
        auto cell=queue.front(); queue.pop();
        const auto& current=grid.at(cell.x,cell.z);
        for (int d=0;d<4;++d) {
            int x=cell.x+maze::dx(d),z=cell.z+maze::dz(d);
            if (!grid.inside(x,z)) require(!current.open[d],"open exterior boundary");
            if (current.picture[d] != maze::WallPicture::none) {
                require(!current.open[d],"picture on opening");
                switch (current.picture[d]) {
                case maze::WallPicture::teapot: ++teapots; break;
                case maze::WallPicture::dx: ++dxPictures; break;
                case maze::WallPicture::yb: ++ybPictures; break;
                default: break;
                }
            }
            if (!current.open[d]) continue;
            require(grid.inside(x,z),"walker can leave maze");
            require(grid.at(x,z).open[maze::opposite(d)],"one-way corridor");
            ++edges;
            if (!seen[z*n+x]) { seen[z*n+x]=true; queue.push({x,z}); }
        }
    }
    for (bool visited:seen) require(visited,"unreachable cell");
    require(edges==2*(n*n-1),"maze is not a perfect tree");
    require(teapots==3,"incorrect number of teapot pictures");
    require(dxPictures<=1 && ybPictures<=1,"duplicate optional wall picture");
    require(grid.at(grid.start.x,grid.start.z).open[grid.startDirection],
            "camera starts facing a wall");
    require(grid.startSign==maze::Point{grid.start.x+maze::dx(grid.startDirection),
                                         grid.start.z+maze::dz(grid.startDirection)},
            "start sign is not ahead of camera");
    require(grid.polyhedra.size()==6 && grid.glSigns.size()==3,"missing obstacles or signs");
    std::vector<bool> occupied(n*n,false);
    auto occupy=[&](maze::Point p) {
        require(grid.inside(p.x,p.z),"object outside maze");
        require(!occupied[p.z*n+p.x],"objects overlap");
        occupied[p.z*n+p.x]=true;
    };
    occupy(grid.start); occupy(grid.startSign);
    for (auto poly:grid.polyhedra) occupy(poly.cell);
    for (auto sign:grid.glSigns) occupy(sign);
    occupy(grid.finish); occupy(grid.ratStart);
    return dxPictures | (ybPictures << 1);
}

int main() {
    try {
        std::array<bool,4> optionalArtCombinations{};
        for (int size:{8,16,32}) for (uint32_t seed=1;seed<=12;++seed) {
            std::mt19937 random(seed);
            maze::Grid grid; grid.generate(size,random);
            optionalArtCombinations[verifyTopology(grid)] = true;
            maze::Walker walker; walker.reset(grid.start,grid.startDirection);
            for (int step=0;step<2000;++step) {
                walker.update(grid,.025f,1.5f,1.5f);
                require(grid.inside(walker.x,walker.z),"walker left grid");
                require(std::isfinite(walker.px) && std::isfinite(walker.yaw),"invalid camera interpolation");
            }
        }
        for (bool seen:optionalArtCombinations)
            require(seen,"optional wall pictures lack a placement combination");
        maze::World world(42); world.reset(8);
        bool regenerated=false;
        for (int step=0;step<20000;++step) {
            if (world.update(.05f,10)) { regenerated=true; break; }
        }
        require(regenerated,"finish did not trigger a new maze");
        std::cout << "maze logic: topology, placement, motion, regeneration passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
