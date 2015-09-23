#include <cv.h>
#include <highgui.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "alphakey.h"

class alphakey
{
public:
    alphakey(int bcnt){
        bytecnt = bcnt;
        outfile = fopen("/tmp/alphakey.bits","wb");
    }
    ~alphakey(){
        fclose(outfile);
    }
    void putbytes(unsigned short val)
    {
        fwrite((unsigned short *)&val,sizeof(unsigned short),1,outfile);
        bytecnt+=sizeof(unsigned short);
    }

    int alhpakeybytes()
    {
        return bytecnt;
    }


private:
    int bytecnt;
    FILE *outfile;
};

using namespace cv;

int yuvalphakey(unsigned char* pInYUV, int w, int h)
{
    Mat src = Mat::zeros(h,w,CV_8UC1);
    Mat dst,bsrc,fsrc;
    alphakey key(0);

    for(int j = 0 ; j < src.rows; ++j) {
        uchar* current  = src.ptr<uchar>(j);
        for(int i= 0;i < src.cols; ++i) {
            current[i] = pInYUV[i+j*src.cols];
        }
    }

    flip(src,fsrc,1);
    blur(fsrc,bsrc,Size(3,3));
    Canny(bsrc, dst, 50, 200, 3);

    std::vector<Vec4i> lines;
    HoughLinesP(dst, lines, 1, CV_PI/180, 50, 50, 10 );

    for( size_t i = 0; i < lines.size(); i++ ) {
        Vec4i l = lines[i];
        line(dst, Point(l[0], l[1]), Point(l[2], l[3]), Scalar(255,255,255), 1, CV_AA);
    }

    for(int y = 0 ; y < src.rows; ++y) {
        uchar* cur  = dst.ptr<uchar>(y);
        for(int x= 0;x < src.cols; ++x) {
            if (cur[x]==255){
                //key.putbytes(y);
                //key.putbytes(x);
            }
        }
    }

    int t = key.alhpakeybytes();

    return t;
}

