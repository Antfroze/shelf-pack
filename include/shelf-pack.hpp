#pragma once

#include <assert.h>
#include <fstream>
#include <optional>
#include <smath.hpp>
#include <vector>

#ifdef SHELFPACK_DEBUG
#include <svg-format.hpp>
using namespace svg_fmt;
#endif

using namespace smath;

const unsigned SHELF_SPLIT_THRESHOLD = 8;
const unsigned ITEM_SPLIT_THRESHOLD = 8;

struct Shelf {
    unsigned x, y, w, h;
    int prev, next, firstItem;
    bool isEmpty;
};

struct Item {
    unsigned x, y, w, h;
    int prev, next, shelf;
    bool allocated;
};

struct Allocation {
    unsigned id;
    RectI rectangle;
};

struct ShelfPackerOptions {
    inline ShelfPackerOptions() : numColumns(1) {}

    unsigned numColumns;
};

class ShelfPacker {
   public:
    inline ShelfPacker(const SizeU& size, const ShelfPackerOptions& opts = ShelfPackerOptions())
        : size(size) {
        shelfWidth = size.x / opts.numColumns;

        Init();
    }

    inline std::optional<Allocation> Allocate(const SizeU& size) {
        if (size.IsEmpty() || size.x > std::numeric_limits<unsigned>::max() &&
                                  size.y > std::numeric_limits<unsigned>::max()) {
            return std::nullopt;
        }

        if (size.x > shelfWidth || size.y > this->size.y) {
            return std::nullopt;
        }

        unsigned width = size.x;
        unsigned height = GetShelfHeight(size.y);
        int selectedShelfHeight, selectedShelf, selectedItem = -1;

        int shelfIdx = firstShelf;
        while (shelfIdx != -1) {
            Shelf& shelf = shelves.at(shelfIdx);

            if (shelf.h < height || shelf.h >= selectedShelfHeight ||
                (!shelf.isEmpty && shelf.h > height + height / 2)) {
                shelfIdx = shelf.next;
                continue;
            }

            int itemIdx = shelf.firstItem;
            while (itemIdx != -1) {
                Item& item = items[itemIdx];

                if (!item.allocated && item.w >= width) {
                    break;
                }
                itemIdx = item.next;
            }

            if (itemIdx != -1) {
                selectedShelf = shelfIdx;
                selectedShelfHeight = shelf.h;
                selectedItem = itemIdx;

                if (shelf.h == height) {
                    // Perfect fit, stop searching.
                    break;
                }
            }

            shelfIdx = shelf.next;
        }

        if (selectedShelf == -1) {
            return std::nullopt;
        }

        Shelf shelf = shelves.at(selectedShelf);
        if (shelf.isEmpty) {
            shelves.at(selectedShelf).isEmpty = false;
        }

        if (shelf.isEmpty && shelf.h > height + SHELF_SPLIT_THRESHOLD) {
            // Split the empty shelf into one of the desired size and a new
            // empty one with a single empty item.

            int newShelfIdx = AddShelf(Shelf{
                shelf.x,
                shelf.y + height,
                0,
                shelf.h - height,
                selectedShelf,
                shelf.next,
                -1,
                true,
            });

            int newItemIdx = AddItem(Item{
                shelf.x,
                0,
                shelfWidth,
                0,
                -1,
                -1,
                newShelfIdx,
                false,
            });

            shelves.at(newShelfIdx).firstItem = newItemIdx;
            int next = shelves.at(selectedShelf).next;
            shelves.at(selectedShelf).h = height;
            shelves.at(selectedShelf).next = newShelfIdx;

            if (next != -1) {
                shelves.at(next).prev = newShelfIdx;
            }
        } else {
            height = shelf.h;
        }

        Item item = items.at(selectedItem);
        if (item.w - width > ITEM_SPLIT_THRESHOLD) {
            int newItemIdx = AddItem(Item{
                item.x + width,
                0,
                item.w - width,
                0,
                selectedItem,
                item.next,
                item.shelf,
                false,
            });

            items.at(selectedItem).w = width;
            items.at(selectedItem).next = newItemIdx;

            if (item.next != -1) {
                items.at(item.next).prev = newItemIdx;
            }
        } else {
            width = item.w;
        }

        items.at(selectedItem).allocated = true;

        unsigned x0 = item.x;
        unsigned y0 = shelf.y;

        RectI rectangle(Vector2I(item.x, shelf.y), Vector2I(width, height));

        allocatedSpace += rectangle.GetArea();

        return std::make_optional(Allocation{
            (unsigned)selectedItem,
            rectangle,
        });
    }

    inline void DeAllocate(int id) {
        Item& item = items.at(id);

        assert(item.allocated);

        Item copy = item;

        item.allocated = false;
        allocatedSpace -= copy.w * shelves.at(item.shelf).h;

        if (copy.next != -1 && !items.at(copy.next).allocated) {
            // Merge the next item into this one.

            Item& next = items.at(copy.next);
            item.next = next.next;
            item.w += next.w;
            copy.w = item.w;

            if (item.next != -1) {
                items[next.next].prev = id;
            }

            // Add next to the free list.
            RemoveItem(copy.next);
        }

        if (copy.prev != -1 && !items.at(copy.prev).allocated) {
            // Merge the item into the previous one.

            Item& prev = items.at(copy.prev);
            prev.next = copy.next;
            prev.w += copy.w;

            if (copy.next != -1) {
                items.at(copy.next).prev = copy.prev;
            }

            // Add item_idx to the free list.
            RemoveItem(id);

            copy.prev = prev.prev;
        }

        if (copy.prev == -1 && copy.next == -1) {
            int shelfIdx = copy.shelf;
            Shelf& shelf = shelves.at(shelfIdx);
            // The shelf is now empty.
            shelf.isEmpty = true;

            // Only attempt to merge shelves on the same column.
            unsigned x = shelf.x;

            int nextShelf = shelf.next;
            if (nextShelf != -1 && shelves.at(nextShelf).isEmpty && shelves.at(nextShelf).x == x) {
                // Merge the next shelf into this one.

                Shelf& next = shelves[nextShelf];
                shelf.next = next.next;
                shelf.h += next.h;

                if (next.next != -1) {
                    shelves.at(next.next).prev = shelfIdx;
                }

                // Add next to the free list.
                RemoveShelf(nextShelf);
            }

            int prevShelfIdx = shelf.prev;
            if (prevShelfIdx != -1 && shelves[prevShelfIdx].isEmpty &&
                shelves[prevShelfIdx].x == x) {
                // Merge the shelf into the previous one.

                Shelf& prev = shelves[prevShelfIdx];
                int nextShelf = shelf.next;

                prev.next = nextShelf;
                prev.h += shelf.h;

                if (nextShelf != -1) {
                    shelves[nextShelf].prev = prevShelfIdx;
                }

                // Add the shelf to the free list.
                RemoveShelf(shelfIdx);
            }
        }
    }

    inline void Clear() { Init(); }

    inline SizeU GetSize() const { return size; }
    inline int GetAllocatedSpace() const { return allocatedSpace; }
    inline int GetFreeSpace() const { return size.Length() - allocatedSpace; }

#ifdef SHELFPACK_DEBUG
    inline void DumpSVG() {
        std::ofstream outputFile("output.svg");    // Open the output file stream

        if (!outputFile.is_open()) {
            std::cerr << "Failed to open file for writing: "
                      << "output.svg" << std::endl;
            return;
        }

        // Write the SVG content to the output file
        outputFile << SVG::Begin(size.x, size.y) << std::endl;
        outputFile << svg_fmt::Rectangle(0, 0, size.x, size.y) << std::endl;

        int shelfIdx = firstShelf;
        while (shelfIdx != -1) {
            Shelf& shelf = shelves.at(shelfIdx);

            int itemIdx = shelf.firstItem;
            while (itemIdx != -1) {
                Item& item = items.at(itemIdx);

                Color color = item.allocated ? Color(70, 70, 180) : Color(50, 50, 50);

                outputFile << svg_fmt::Rectangle(item.x, shelf.y, item.w, shelf.h)
                                  .WithFill(color)
                                  .WithStroke(Stroke(1, Color::Black()))
                           << std::endl;

                itemIdx = item.next;
            }

            shelfIdx = shelf.next;
        }

        outputFile << SVG::End() << std::endl;

        outputFile.close();    // Close the output file stream
    }
#endif

   private:
    inline void Init() {
        assert(size.x > 0 && size.y > 0);
        assert(size.x <= std::numeric_limits<unsigned>::max() &&
               size.y <= std::numeric_limits<unsigned>::max());

        shelves.clear();
        items.clear();

        unsigned numColumns = size.x / shelfWidth;
        int prev = -1;

        for (int i = 0; i < numColumns; ++i) {
            int firstItem = items.size();
            unsigned x = i * shelfWidth;
            int next = i + 1 < numColumns ? i + 1 : -1;

            shelves.emplace_back(Shelf{
                x,
                0,
                0,
                size.y,
                prev,
                next,
                firstItem,
                true,
            });

            items.emplace_back(Item{x, 0, shelfWidth, 0, -1, -1, i, false});

            prev = i;
        }

        firstShelf = allocatedSpace = 0;
        freeItems = freeShelves = -1;
    }

    inline int AddItem(Item item) {
        if (freeItems != -1) {
            int idx = freeItems;
            freeItems = items.at(idx).next;
            items[idx] = item;

            return idx;
        }

        int idx = items.size();
        items.emplace_back(item);

        return idx;
    }

    inline int AddShelf(Shelf shelf) {
        if (freeShelves != -1) {
            int idx = freeShelves;
            freeShelves = shelves.at(idx).next;
            shelves[idx] = shelf;

            return idx;
        }

        int idx = shelves.size();
        shelves.emplace_back(shelf);

        return idx;
    }

    inline void RemoveItem(int idx) {
        items.at(idx).next = freeItems;
        freeItems = idx;
    }

    inline void RemoveShelf(int idx) {
        RemoveItem(shelves.at(idx).firstItem);
        shelves.at(idx).next = freeShelves;
        freeShelves = idx;
    }

    inline int GetShelfHeight(int size) {
        int alignment;

        if (size >= 0 && size <= 31) {
            alignment = 8;
        } else if (size >= 32 && size <= 127) {
            alignment = 16;
        } else if (size >= 128 && size <= 511) {
            alignment = 32;
        } else {
            alignment = 64;
        }

        int adjusted_size = size;
        int rem = size % alignment;

        if (rem > 0) {
            adjusted_size = size + alignment - rem;
            if (adjusted_size > this->size.y) {
                adjusted_size = size;
            }
        }

        return adjusted_size;
    }

    std::vector<Shelf> shelves;
    std::vector<Item> items;

    SizeU size;
    int firstShelf;
    int freeItems, freeShelves = -1;
    unsigned shelfWidth;
    int allocatedSpace;
};