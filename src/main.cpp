#define SHELFPACK_DEBUG
#include <shelf-pack.hpp>

int main() {
    ShelfPacker pack(SizeU(200, 200));

    auto alloc = pack.Allocate(SizeU(20, 20));
    pack.Allocate(SizeU(20, 20));
    pack.Allocate(SizeU(20, 20));
    auto alloc2 = pack.Allocate(SizeU(45, 12));
    pack.Allocate(SizeU(20, 20));
    pack.DeAllocate(alloc->id);
    pack.DeAllocate(alloc2->id);

    pack.DumpSVG();

    return 0;
}
