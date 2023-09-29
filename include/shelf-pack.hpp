#pragma once

#include <fstream>
#include <map>
#include <vector>

#ifdef SHELFPACK_DEBUG
#include <svg-format.hpp>
using namespace svg_fmt;
#endif

struct Bin {
    inline Bin(int id, int x, int y, int w, int h, int maxW = -1, int maxH = -1)
        : id(id), x(x), y(y), w(w), h(h), maxW(maxW), maxH(maxH) {
        this->maxW = maxW == -1 ? w : maxW;
        this->maxH = maxH == -1 ? h : maxH;
    }

    int id;
    int x, y;
    int w, h, maxW, maxH;
    int refCount;
};

struct Shelf {
    inline Shelf(int y, int w, int h) : x(0), y(y), w(w), h(h), free(w) {}

    inline Bin* Allocate(int w, int h, int id = -1) {
        if (w > free || h > this->h) {
            return nullptr;
        }

        int x = this->x;
        this->x += w;
        this->free -= w;
        return new Bin(id, x, this->y, w, h, w, this->h);
    }

    inline void Resize(int w) {
        this->free += (w - this->w);
        this->w = w;
    }

    int x, y;
    int w, h;
    int free;
};

struct PackerOptions {
    inline PackerOptions() : autoResize(false) {}

    bool autoResize;
};

class ShelfPacker {
   public:
    inline ShelfPacker(int w, int h, const PackerOptions& options = PackerOptions())
        : w(w), h(h), options(options) {}

    inline Bin* GetBin(int id) { return bins.at(id); }
    inline int Ref(Bin& bin) {
        if (++bin.refCount == 1) {    // a new Bin.. record height in stats histogram..
            stats[bin.h] = (stats[bin.h] | 0) + 1;
        }

        return bin.refCount;
    };
    inline int Unref(Bin& bin) {
        if (bin.refCount == 0) {
            return 0;
        }

        if (--bin.refCount == 0) {
            this->stats[bin.h]--;
            bins.erase(bin.id);
            this->freeBins.push_back(&bin);
        }

        return bin.refCount;
    }

    Bin* PackOne(int w, int h, int id = -1) {
        int y = 0;
        int waste = 0;

        struct {
            Shelf* shelf = nullptr;
            Bin* freebin = nullptr;
            int waste = std::numeric_limits<std::int32_t>::max();
        } best;

        // if id was supplied, attempt a lookup..
        if (id != -1) {
            Bin* pbin = GetBin(id);
            if (pbin) {    // we packed this bin already
                Ref(*pbin);
                return pbin;
            }
            maxId = std::max(id, maxId);
        } else {
            id = ++maxId;
        }

        // First try to reuse a free bin..
        for (const auto& bin : freeBins) {
            // exactly the right height and width, use it..
            if (h == bin->maxH && w == bin->maxW) {
                return allocFreebin(bin, w, h, id);
            }
            // not enough height or width, skip it..
            if (h > bin->maxH || w > bin->maxW) {
                continue;
            }
            // extra height or width, minimize wasted area..
            if (h <= bin->maxH && w <= bin->maxW) {
                waste = (bin->maxW * bin->maxH) - (w * h);
                if (waste < best.waste) {
                    best.waste = waste;
                    best.freebin = bin;
                }
            }
        }

        // Next find the best shelf..
        for (auto& shelf : shelves) {
            y += shelf.h;

            // not enough width on this shelf, skip it..
            if (w > shelf.free) {
                continue;
            }

            // exactly the right height, pack it..
            if (h == shelf.h) {
                return allocShelf(shelf, w, h, id);
            }

            // not enough height, skip it..
            if (h > shelf.h) {
                continue;
            }

            // extra height, minimize wasted area..
            if (h < shelf.h) {
                waste = (shelf.h - h) * w;
                if (waste < best.waste) {
                    best.waste = waste;
                    best.shelf = &shelf;
                }
            }
        }
        if (best.freebin != nullptr) {
            return allocFreebin(best.freebin, w, h, id);
        }

        if (best.shelf != nullptr) {
            return allocShelf(*best.shelf, w, h, id);
        }

        // No free bins or shelves.. add shelf..
        if (h <= (this->h - y) && w <= this->w) {
            shelves.emplace_back(y, this->w, h);
            return allocShelf(shelves.back(), w, h, id);
        }

        // No room for more shelves..
        // If `autoResize` option is set, grow the sprite as follows:
        //  * double whichever sprite dimension is smaller (`w1` or `h1`)
        //  * if sprite dimensions are equal, grow width before height
        //  * accomodate very large bin requests (big `w` or `h`)
        if (options.autoResize) {
            int h1, h2, w1, w2;

            h1 = h2 = this->h;
            w1 = w2 = this->w;

            if (w1 <= h1 || w > w1) {    // grow width..
                w2 = std::max(w, w1) * 2;
            }
            if (h1 < w1 || h > h1) {    // grow height..
                h2 = std::max(h, h1) * 2;
            }

            Resize(w2, h2);
            return PackOne(w, h, id);    // retry
        }

        return nullptr;
    }

    inline void Resize(int w, int h) {
        this->w = w;
        this->h = h;
        for (auto& shelf : shelves) {
            shelf.Resize(w);
        }
    }

#ifdef SHELFPACK_DEBUG
    inline void DumpSVG() {
        std::ofstream outputFile("output.svg");    // Open the output file stream

        if (!outputFile.is_open()) {
            std::cerr << "Failed to open file for writing: "
                      << "output.svg" << std::endl;
            return;
        }

        // Write the SVG content to the output file
        outputFile << SVG::Begin(w, h) << std::endl;
        outputFile << Rectangle(0, 0, w, h) << std::endl;

        for (const auto& kv : bins) {
            const Bin& bin = *kv.second;
            outputFile << Rectangle(bin.x, bin.y, bin.w, bin.h)
                              .WithFill(Color::Red())
                              .WithStroke(Stroke(2, Color::Blue()))
                       << std::endl;
        }

        outputFile << SVG::End() << std::endl;

        outputFile.close();    // Close the output file stream
    }
#endif

   private:
    inline Bin* allocFreebin(Bin* bin, int w, int h, int id) {
        freeBins.erase(std::remove(freeBins.begin(), freeBins.end(), bin), freeBins.end());
        bin->id = id;
        bin->w = w;
        bin->h = h;
        bin->refCount = 0;
        bins[id] = bin;
        Ref(*bin);
        return bin;
    }

    inline Bin* allocShelf(Shelf& shelf, int w, int h, int id) {
        Bin* pbin = shelf.Allocate(w, h, id);

        if (pbin) {
            bins[id] = pbin;
            Ref(*pbin);
        }
        return pbin;
    }

    int w, h;
    const PackerOptions& options;

    std::vector<Shelf> shelves;
    std::vector<Bin*> freeBins;
    std::map<int, Bin*> bins;
    std::map<int, int> stats;
    int maxId = 0;
};
