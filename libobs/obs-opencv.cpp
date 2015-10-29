#include "obs-opencv.h"
#include "obs-internal.h"

#include <libyuv.h>
#include <cv.h>
#include <highgui.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

void update_mix_frame(struct obs_source *source)
{
    while (source) {
        struct obs_source_frame * frame = source->cur_async_frame;
        if ( frame ) {
            source->cur_mix_frame = frame;
        }
        source = (struct obs_source*)source->context.next;
    }
}

void blending_sources(struct obs_source *source)
{
    update_mix_frame(source);

    double alpha = 0.5;

    cv::Mat mix;
    bool initilized = false;

    struct obs_source * first_source = NULL;

    while (source) {
        struct obs_source_frame * frame = source->cur_mix_frame;
        if (frame) {
            if ( !initilized ) {
                mix = cv::Mat::zeros(source->cur_mix_frame->height,
                                     source->cur_mix_frame->width,
                                     CV_8UC4);
                initilized = true;
            }
            cv::Mat dst = cv::Mat::zeros(frame->height, frame->width, CV_8UC4);
            //uint8_t * data = new uint8_t[frame->height * frame->width * 4];
            assert(dst.isContinuous());
            libyuv::UYVYToARGB(frame->data[0],
                               frame->linesize[0],
                               dst.ptr(),
                               //data,
                               frame->width * 4,
                               frame->width,
                               frame->height);
            //cv::Mat dst(frame->height, frame->width, CV_8UC4, data);
            //imshow(source->context.name, dst);

            addWeighted( mix, alpha, dst, 1-alpha, 0.0, mix);

            first_source = source;
        }

        source = (struct obs_source*)source->context.next;
    }

    if ( initilized ) {
        imshow("opencv", mix);
    }

    //quick and dirty way: replace cur_async_frame with mixed frame
    if ( first_source ) {
        struct obs_source_frame * frame = first_source->cur_async_frame;
        if (frame) {
            libyuv::ARGBToUYVY(mix.ptr(),
                               frame->width * 4,
                               frame->data[0],
                               frame->linesize[0],
                               frame->width,
                               frame->height);
        }
    }
}
