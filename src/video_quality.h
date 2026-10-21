/* Resolution is a preference: choose the best compatible track at/below it,
 * otherwise the smallest compatible track above it. Never upscale or transcode. */
static int video_quality_better(int width,int height,int target,int best_w,int best_h){
    if(width<1||height<1||width>1920||height>1080)return 0;
    if(!best_h)return 1;
    int fits=height<=target,old_fits=best_h<=target;
    if(fits!=old_fits)return fits;
    return fits?width*height>best_w*best_h:width*height<best_w*best_h;
}
