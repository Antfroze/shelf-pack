#pragma once

#include <assert.h>
#include <fstream>
#include <optional>
#include <smath.hpp>
#include <svg-format.hpp>
#include <vector>

using namespace svg_fmt;
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
    unsigned generation;
};

struct Allocation {
    unsigned id;
    RectI rectangle;
};

struct ShelfPackerOptions {
    inline ShelfPackerOptions() : numColumns(1) {}

    unsigned numColumns;
};

struct ShelfPacker {
    inline ShelfPacker(const SizeU& size, const ShelfPackerOptions& opts = ShelfPackerOptions())
        : size(size) {
        shelfWidth = size.x / opts.numColumns;

        Init();
    }

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

            items.emplace_back(Item{x, 0, shelfWidth, 0, -1, -1, i, false, 1});

            prev = i;
        }

        firstShelf = allocatedSpace = 0;
        freeItems = freeShelves = -1;
    }

    inline std::optional<Allocation> PackOne(const SizeU& size) {
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
                1,
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
                1,
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
        unsigned generation = items.at(selectedItem).generation;

        unsigned x0 = item.x;
        unsigned y0 = shelf.y;

        RectI rectangle(Vector2I(item.x, shelf.y), Vector2I(width, height));

        allocatedSpace += rectangle.GetArea();

        return std::make_optional(Allocation{
            selectedItem | generation << 16,
            rectangle,
        });
    }

    int AddItem(Item item) {
        if (freeItems != -1) {
            int idx = freeItems;
            item.generation = items.at(idx).generation += 1;
            freeItems = items.at(idx).next;
            items[idx] = item;

            return idx;
        }

        int idx = items.size();
        items.emplace_back(item);

        return idx;
    }

    int AddShelf(Shelf shelf) {
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

    int GetShelfHeight(int size) {
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

    std::vector<Shelf> shelves;
    std::vector<Item> items;

    Vector2U size;
    int firstShelf;
    int freeItems, freeShelves = -1;
    unsigned shelfWidth;
    int allocatedSpace;
};