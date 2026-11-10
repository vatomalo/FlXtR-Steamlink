#include <assert.h>
#include <stdio.h>
#include "../src/video_quality.h"
int main(void){
    assert(video_quality_better(1280,720,480,0,0));
    assert(video_quality_better(1280,720,480,1920,1080));
    assert(!video_quality_better(1920,1080,480,1280,720));
    assert(video_quality_better(854,480,480,1280,720));
    assert(!video_quality_better(1280,720,480,854,480));
    assert(video_quality_better(1920,1080,1080,1280,720));
    assert(!video_quality_better(3840,2160,1080,0,0));
    puts("PASS: preferred resolution, unavailable 480p fallback, 1080p selection and hardware ceiling");
}
