#include <shelf-pack.hpp>

int main() {
    ShelfPacker pack(Size2U(200, 200));

    pack.PackOne(Size2U(20, 20));
    pack.PackOne(Size2U(20, 20));
    pack.PackOne(Size2U(20, 20));
    pack.PackOne(Size2U(45, 12));

    pack.DumpSVG();

    return 0;
}
