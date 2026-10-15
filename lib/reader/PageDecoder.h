#pragma once
#include "ReaderTypes.h"
namespace reader {
struct PageFit { int width=0,height=0,x=0,y=0; };
PageFit fit_page(int width,int height,int canvas_width=480,int canvas_height=800);
class PageDecoder {
public:
    explicit PageDecoder(SdCard &card) : card(card) {}
    PageDecoder(const PageDecoder &) = delete;
    PageDecoder &operator=(const PageDecoder &) = delete;
    ~PageDecoder() { release(); }
    bool decode(const Page &page);
    void release();
    const uint16_t *pixels() const { return front; }
    const std::string &error() const { return failure; }
    // Codec adapter output; never publishes partial pixels to LVGL.
    bool prepare(int original_width,int original_height,int decoded_width,int decoded_height);
    bool block(int x,int y,int width,int height,int stride,const uint16_t *pixels);
    SdCard &storage() { return card; }
private:
    SdCard &card;
    uint16_t *front=nullptr,*staging=nullptr;
    PageFit fit;
    int source_width=0,source_height=0;
    std::string failure;
};
bool decode_jpeg(PageDecoder &output,const std::string &path,std::string &error);
bool decode_png(PageDecoder &output,const std::string &path,std::string &error);
}
