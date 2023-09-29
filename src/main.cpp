#include <shelf-pack.hpp>

int main() {
    ShelfPacker pack(1024, 1024);

    pack.PackOne(20, 20);
    pack.PackOne(20, 20);
    pack.PackOne(20, 20);
    pack.PackOne(23, 15);
    pack.PackOne(32, 20);
    pack.PackOne(10, 23);
    pack.PackOne(23, 15);
    pack.PackOne(10, 57);

    return 0;
}
