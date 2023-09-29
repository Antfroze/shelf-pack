#include <shelf-pack.hpp>

int main() {
    ShelfPacker pack(SizeU(200, 200));

    pack.PackOne(SizeU(20, 20));
    pack.PackOne(SizeU(20, 20));
    pack.PackOne(SizeU(20, 20));
    pack.PackOne(SizeU(45, 12));

    pack.DumpSVG();

    return 0;
}
