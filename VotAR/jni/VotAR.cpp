/*
 * Started from "ivo" very useful post helpful post :
 * http://stackoverflow.com/questions/2881939/android-read-png-image-without-alpha-and-decode-as-argb-8888
 *
 */

#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <math.h>
#include <utility>
#include <endian.h>
#include <stdlib.h>
#include <time.h>

#define  LOG_TAG    "nativeAnalyze"
#define  Log_i(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  Log_e(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

extern "C" {

float startTime;


void benchmarkStart() {
	startTime = (float)clock()/CLOCKS_PER_SEC;
	Log_i("Benchmark: 0.000 | Starting");
}
void benchmarkElapsed(const char *text) {
	float endTime = (float)clock()/CLOCKS_PER_SEC;

	float timeElapsed = endTime - startTime;
	Log_i("Benchmark: %8f | %s", timeElapsed, text);
}


inline void swap(float *a, float *b) {
	float tmp=*a;
	*a=*b;
	*b=tmp;
}

#define max(a,b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b; })

#define min(a,b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b; })


// will not work on first/last column/work but that's ok : those are not useful pixels
// we just skip some at the beginning / end just to make sure we don't SIGSEGV
// on reading outer pixels
// we need to average each component separately to avoid overflowing on others
// will only work if inpixels != outpixels because the average method need stable neighbors
void average33(unsigned int *inpixels, unsigned int *outpixels,  unsigned int width, unsigned int height) {
	unsigned int pixelcount=width*height;
	for (int i=width+1; i<pixelcount-width-1; i++) {
		outpixels[i]= (
					(((inpixels[i-1-width] & 0x00FF0000) + (inpixels[i-width] & 0x00FF0000) + (inpixels[i+1-width] & 0x00FF0000)	// Row -1, component 1
					+ (inpixels[i-1]       & 0x00FF0000) + (inpixels[i]       & 0x00FF0000) + (inpixels[i+1]       & 0x00FF0000)	// Row  0, component 1
					+ (inpixels[i-1+width] & 0x00FF0000) + (inpixels[i+width] & 0x00FF0000) + (inpixels[i+1+width] & 0x00FF0000))	// Row +1, component 1
					/ 9) & 0x00FF0000)
				|
					(
					(((inpixels[i-1-width] & 0x0000FF00) + (inpixels[i-width] & 0x0000FF00) + (inpixels[i+1-width] & 0x0000FF00)	// Row -1, component 2
					+ (inpixels[i-1]       & 0x0000FF00) + (inpixels[i]       & 0x0000FF00) + (inpixels[i+1]       & 0x0000FF00)	// Row  0, component 2
					+ (inpixels[i-1+width] & 0x0000FF00) + (inpixels[i+width] & 0x0000FF00) + (inpixels[i+1+width] & 0x0000FF00))	// Row +1, component 2
					/ 9) & 0x0000FF00)
				|
					(
					(((inpixels[i-1-width] & 0x000000FF) + (inpixels[i-width] & 0x000000FF) + (inpixels[i+1-width] & 0x000000FF)	// Row -1, component 3
					+ (inpixels[i-1]       & 0x000000FF) + (inpixels[i]       & 0x000000FF) + (inpixels[i+1]       & 0x000000FF)	// Row  0, component 3
					+ (inpixels[i-1+width] & 0x000000FF) + (inpixels[i+width] & 0x000000FF) + (inpixels[i+1+width] & 0x000000FF))	// Row +1, component 3
					/ 9) & 0x000000FF)
				| 0xFF000000;
		;
	}
}

// using http://lolengine.net/blog/2013/01/13/fast-rgb-to-hsv algorithm from "sam"
void rgb2hsv(unsigned int *pixels, unsigned int pixelcount) {
	for (int i=0 ; i<pixelcount ; i++) {
		float K = 0.f;
		float r,g,b;
		unsigned int h,s,v;
		unsigned int *currentpixel=&(pixels[i]);

		// 0xFF0000FF=red
		// 0xFF00FF00=green
		// 0xFFFF0000=blue
		r = ((float)( *currentpixel        & 0x000000FF)) / 255.0f;
		g = ((float)((*currentpixel >> 8)  & 0x000000FF)) / 255.0f;
		b = ((float)((*currentpixel >> 16) & 0x000000FF)) / 255.0f;

		if (g < b)
		{
			swap(&g, &b);
			K = -1.f;
		}

		if (r < g)
		{
			swap(&r, &g);
			K = -2.f / 6.f - K;
		}

		float chroma = r - min(g, b);

		h = (int) (fabs(K + (g - b) / (6.f * chroma + 1e-20f)) * 255.0f);
		s = (int) ((chroma / (r + 1e-20f)) * 255.0f);
		v = (int) (r * 255.0f);
		*currentpixel= 0xFF000000 | (( v << 16 ) & 0x00FF0000) | (( s << 8 ) & 0x0000FF00)| (h & 0x000000FF);
	}
}



// combine many operation to get an easier to work on image :
// - allocate memory for a new image
// - average pixels in 3x3 squares into the new "working image"
// - convert colorspaces (rgb2hsv)
// - return the resulting image
unsigned int *generateWorkingImage(unsigned int *inpixels, unsigned int width, unsigned int height) {
	unsigned int pixelcount = width * height;
	unsigned int *workpixels = (unsigned int*) malloc(sizeof(int) * pixelcount);
	if (!workpixels) {
		Log_e("Failed to allocate %d bytes as a work image",pixelcount);
		return workpixels;
	}
	benchmarkElapsed("malloc workpixels");

	average33(inpixels, workpixels, width, height);
	benchmarkElapsed("average33");
//	rgb2hsv(workpixels, pixelcount);
//	benchmarkElapsed("rgb2hsv");
	return workpixels;
}


#define PIXEL_STEP_TO_CENTER				5

// we would tolerate for a good square :
//   + component  : 0x20
//   + saturation : 0x20
// * 4 (for each subsquare)
// = 0x80
#define AVERAGE_DIFFERENCE_ALLOWED			(0xB8)

#define SATURATION_NOMINATOR				(0x800)
#define SATURATION_V_OFFSET					(0x10)
#define SATURATION_H_OFFSET					(0x01)



#define HUEDIFF_MINIMUM_RECOMMENDED			(0x10)
#define HUEDIFF_EXPONENT_DIVISOR			(0x100)
#define HUEDIFF_LINEAR_DENOMINATOR			(0x04)

int deltaMinMax(int x, int y, int z)
{
	int min,max;
	if (x<=y) {
		if (x<=z) {
			min=x;
			if (y<=z)
				max=z;
			else
				max=y;
		} else {
			min=z;
			max=y;
		}
	}
	else if (x<=z) {
		max=z;
		min=y;
	}
	else {
		max=x;
		if(y<=z)
			min=y;
		else
			min=z;
	}
	return max-min;
}

// simple function to put a green dot on an image position
void markPixel(unsigned int *pixels, unsigned int width, unsigned int height, unsigned int x, unsigned int y, unsigned int color, unsigned int size) {
	int pixelcount=width*height;
	for (int j=y-size; j<y+size; j++)
		for (int i=x-size; i<x+size; i++) {
			int pixelindex=i+j*width;
			// make sure no memory overflow in this loop (the mark can be close to top/bottom edge and radius bigger than PIXEL_STEP_TO_CENTER)
			if (pixelindex>=0 && pixelindex<pixelcount)
				pixels[pixelindex]=color;
		}
}

#define ALGO_STATS

#ifdef ALGO_STATS
int algo_stats_hues[4];
int algo_stats_sat[4];
int algo_stats_mindiff, algo_stats_minpr;
#endif


/*
 * return the color delta
 * the function is somewhat long but will be called almost pixelcount time so inline is probably worth it
 */
inline int checkSquare(unsigned int c, unsigned int cindex) {
/*	const static unsigned int rcc[4][3]={ // reference colors components clockwise
		//    R    G    B
			{0xFF,0xFF,0x00},		// yellow
			{0x00,0xFF,0xFF},		// cyan
			{0xFF,0x00,0xFF},		// magenta
			{0x00,0x00,0xFF},		// blue
		};*/

	// maths : https://www.desmos.com/calculator/k8ihlpeo89

	// we skip burned pixels
	if (!(c & 0xFF000000))
		return AVERAGE_DIFFERENCE_ALLOWED*2;

	// color components
	int		r = (int)(c & 0x000000FF),
			g = (int)((c >> 8) & 0x000000FF),
			b = (int)((c >> 16) & 0x000000FF);

	// we have the sum and each components multiplied by 3, so the sum is like an average
	//int csum=r+g+b;
	int diff=0;

	// relative saturation, allow negative values (if we have the opposite hue)
	int rsat=0;
	// absolute saturation
//	int sat=deltaMinMax(r,g,b);
	int huediff;


	switch (cindex) {
	case 0 :
		// yellow,
		// r and g are strong, b is weak


		// first, outcast anything where b is not the weakest color
		// which means dead square
		//
		// if not, we have something like :
		// 0     n      n'    255
		// .. b .... g .... r ..
		//     or
		// .. b .... r .... g ..
		// are g and r close together or one of them is close to b ?
		if (b>r || b>g)
			huediff=0x400;
		else
			huediff=abs(r-g)*0x100/(r+g+1);

		// we assume one of the strong component > weak to calculate a relative saturation
		// if it's not the case, we have a "negative" saturation, which is mean it has a completely reversed hue
		rsat=max(r,g)-b;

		break;
	case 1 :
		// cyan,
		// g and b are strong, r is weak
		if (r>g || r>b)
			huediff=0x400;
		else
			huediff=abs(g-b)*0x100/(g+b+1);
		rsat=max(g,b)-r;

		break;
	case 2 :
		// magenta,
		// r and b are strong, g is weak
		if (g>r || g>b)
			huediff=0x400;
		else
			huediff=abs(r-b)*0x100/(r+b+1);
		rsat=max(r,b)-g;

		break;
	case 3 :
		// blue,
		// r and g are weak, b is strong
		if (b<r || b<g)
			huediff=0x400;
		else
			huediff=abs(r-g)*0x100/(r+g+1);

		// manual adjustment: blue is a single component color
		// and has been found to be much darker in photos experiments, giving it less contrast
		// so we give it a noticeable boost in contrast
		rsat=b-min(r,g)+5;
		break;
	}
	huediff-=HUEDIFF_MINIMUM_RECOMMENDED;
	if (huediff>0) {
		diff+=(huediff*huediff/HUEDIFF_EXPONENT_DIVISOR)+(huediff/HUEDIFF_LINEAR_DENOMINATOR);
	}
#ifdef ALGO_STATS
	algo_stats_hues[cindex]=diff;
#endif

	// adjusted 1/sat curve :
	// extremely low saturation = dead square
	// very low sat = high penalize much
	// low, medium sat = penalize very little (to still work in dark rooms
	// high sat = don't penalize, might give a small bonus
	if (rsat>=0) {
		// a quite soft exponential curve for low values : x * x / 0x80
//old formula :		diff+=(rsat*rsat/SATURATION_EXPONENT_DIVISOR) + (rsat/SATURATION_LINEAR_DIVISOR);
		diff+=SATURATION_NOMINATOR/(rsat+SATURATION_H_OFFSET)-SATURATION_V_OFFSET;
	} else {
		diff+=AVERAGE_DIFFERENCE_ALLOWED;
	}
#ifdef ALGO_STATS
	if (rsat>0) {
		algo_stats_sat[cindex]=SATURATION_NOMINATOR/(rsat+SATURATION_H_OFFSET)-SATURATION_V_OFFSET;
	} else {
		algo_stats_sat[cindex]=AVERAGE_DIFFERENCE_ALLOWED;
	}
#endif

	return diff;
}

int findOnePattern(unsigned int *workpixels, unsigned int width, unsigned int height, unsigned int x, unsigned int y,unsigned int *inpixels) {
	unsigned int uc[4]; // unshifted colors
	// Yellow, Cyan, Magenta, Blue
	unsigned int ct=x+width*y;
	const unsigned int hstep=PIXEL_STEP_TO_CENTER;
	unsigned int vstep=width*hstep;

	// we use x/y as the center of this pattern, each table cell match a sub-square
	//  0 | 1
	// ---X---
	//  3 | 2
	uc[0]=workpixels[ct-vstep-hstep]; // top left
	uc[1]=workpixels[ct-vstep+hstep]; // top right
	uc[2]=workpixels[ct+vstep+hstep]; // bot right
	uc[3]=workpixels[ct+vstep-hstep]; // bot left




	// the fun continues
	// pr is the pattern rotation, we loop on the 4 possible rotations of the first pattern
	//  pr=0         pr=1         pr=2          pr=3
	//  0 | 1        1 | 2        2 | 3         3 | 0
	// -------  ->  -------  ->  --------  ->  -------
	//  3 | 2        0 | 3        1 | 0         2 | 1
	for (int pr=0; pr<4; pr++) {
		int diff=0;
		// for each sub-square of this pattern rotation, add up the difference with the reference color
#ifdef ALGO_STATS
		algo_stats_mindiff=0xFFFFFF;
		algo_stats_minpr=0;
		algo_stats_hues[0]=0; algo_stats_hues[1]=0; algo_stats_hues[2]=0; algo_stats_hues[3]=0;
		algo_stats_sat[0]=0; algo_stats_sat[1]=0; algo_stats_sat[2]=0; algo_stats_sat[3]=0;
#endif
		for (int i=0; i<4; i++) {
			// check every square
			// pr=0
			//  Y |            | C          |             |
			// -------  ->  -------  ->  --------  ->  -------
			//    |            |            | M         B |
			//
			// pr=1
			//  B |            | Y          |             |
			// -------  ->  -------  ->  --------  ->  -------
			//    |            |            | C         M |
			//
			// ...
			diff+=checkSquare(uc[(pr+i)%4], i);
#ifndef ALGO_STATS
			// if the color difference for all the subsquares is too big, it's over for this pattern rotation so don't waist time
			if (diff>AVERAGE_DIFFERENCE_ALLOWED)
				break;
#endif

		}
#ifdef ALGO_STATS
		if (diff<algo_stats_mindiff) {
			algo_stats_mindiff=diff;
			algo_stats_minpr=pr;
		}
		if (diff-30<=AVERAGE_DIFFERENCE_ALLOWED) {
			Log_i("Square pr:%d | d:%d | hues:%d,%d,%d,%d | sat:%d,%d,%d,%d | pos: %d,%d",algo_stats_minpr,algo_stats_mindiff*100/AVERAGE_DIFFERENCE_ALLOWED,
						algo_stats_hues[0]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_hues[1]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_hues[2]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_hues[3]*100/AVERAGE_DIFFERENCE_ALLOWED,
						algo_stats_sat[0]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_sat[1]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_sat[2]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_sat[3]*100/AVERAGE_DIFFERENCE_ALLOWED,
						x,y);
			if (diff>AVERAGE_DIFFERENCE_ALLOWED) {
				markPixel(inpixels,width, height, x, y,0xFFFFFFFF,4);
			}
		}
#endif

		//
		if (diff<=AVERAGE_DIFFERENCE_ALLOWED) {
			return pr;
		}
	}
	return -1;
}



int findAllPatterns(unsigned int *inpixels, unsigned int *workpixels, unsigned int width, unsigned int height) {
	// the fun part start here... bruteforce every position
	//          i=
	//        5 7 9
	// j=5 -> + . .     . + .    . . +    . . .
	// j=7 -> . . .  -> . . . -> . . . -> + . .
	// j=9 -> . . .     . . .    . . .    . . .

	int prcount[4]={0,0,0,0};
	for (int j=PIXEL_STEP_TO_CENTER; j<height-PIXEL_STEP_TO_CENTER; j+=2) {
		for (int i=PIXEL_STEP_TO_CENTER; i<width-PIXEL_STEP_TO_CENTER; i+=2) {
			//markPixel(inpixels,width, height, i, j);
			int pr=findOnePattern(workpixels, width, height, i,j,inpixels);
			if (pr>=0) {
				prcount[pr]++;
				markPixel(inpixels,width, height, i, j,0xFF00FF00,3+width/512);
				// also burn the workpixels to make sure we do not count the same square 2 times
				markPixel(workpixels,width, height, i, j,0x00000000,7+width/1024);
			}
		}
	}
	Log_i("found patterns... 1: %d | 2: %d | 3: %d | 4: %d ", prcount[0], prcount[1], prcount[2], prcount[3]);
	return -1;
}






JNIEXPORT jint JNICALL Java_com_poinsart_votar_MainAct_nativeAnalyze(JNIEnv *env, jclass reserved, jobject bitmap)
{
	AndroidBitmapInfo info;
	unsigned int *pixels, *workpixels;
	unsigned int width, height, pixelcount;


	Log_i("Now in nativeAnalyze code");
	benchmarkStart();



	if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
		Log_e("Failed to get Bitmap info");
		return -1;
	}
	width=info.width;
	height=info.height;
	pixelcount=width*height;
	Log_i("Handling Bitmap in native code... Width: %d, Height: %d", width, height);

	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		Log_e("Incompatible Bitmap format");
		return -1;
	}

	void** voidpointer=(void**) &pixels;
	if (AndroidBitmap_lockPixels(env, bitmap, voidpointer) < 0) {
		Log_e("Failed to lock the pixels of the Bitmap");
		return -1;
	}

	benchmarkElapsed("various initialization stuff");
	workpixels=generateWorkingImage(pixels, width, height);
	if (!workpixels)
		return -1;

	findAllPatterns(pixels,workpixels,width, height);
	benchmarkElapsed("findAllPatterns");
	free(workpixels);

	//for (int i=0;i<(pixelcount/4);i++) {
	//	pixels[i]=0xFFFF0000;
	//}

	if(AndroidBitmap_unlockPixels(env, bitmap) < 0) {
		Log_e("Failed to unlock the pixels of the Bitmap");
		return -1;
	}

	return 0;
}
}
