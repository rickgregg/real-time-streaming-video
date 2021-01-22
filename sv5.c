/*
** sv5.c
** write streaming video from a webcam into the framebuffer for 
** real-time display on beaglebone black
**
** april 29, 2018 - rlg
*/

/*
** NOTE: need to run as root in tty1 (chvt 1) because framebuffer is at
** the linux kernel level and only available in tty
**
** DEBUG compile using: gcc -g3 sv5.c -o sv5 -lrt
** OPTIMIZE compile using: gcc -O3 sv5.c -o sv5 -lrt
**
** set cpu frequency governor to "performance" on start-up
** echo userspace > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
*/

#include <linux/fb.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/videodev2.h>

/*
** 4D LCD display resolution 480 x 272
** HVGA (Half-size VGA) screens have 480×320 pixels (3:2 aspect ratio), 
** 480×360 pixels (4:3 aspect ratio), 480×272 (~16:9 aspect ratio) 
** or 640×240 pixels (8:3 aspect ** ratio). 
*/
#define	HVGA_WIDTH	480
#define HVGA_HEIGHT	272 
#define FRAMEBUF_SIZE	HVGA_WIDTH*HVGA_HEIGHT*2
/*
** WQVGA resolution is 432 x 240
** Logitech c920 USB Camera
*/
#define	WQVGA_WIDTH	432
#define WQVGA_HEIGHT	240
#define FILEBUF_SIZE	WQVGA_WIDTH*WQVGA_HEIGHT*2
#define RGB565_SIZE 	WQVGA_WIDTH*WQVGA_HEIGHT*2
#define RGB888_SIZE 	WQVGA_WIDTH*WQVGA_HEIGHT*3
#define YUYV_SIZE   	WQVGA_WIDTH*WQVGA_HEIGHT*2
#define GRAYSCALE_SIZE	WQVGA_WIDTH*WQVGA_HEIGHT
/*
** SVGA resolution is 800 x 600
*/
#define SVGA_WIDTH	800
#define SVGA_HEIGHT	600
/*
** WUXGA resolution is 1920 x 1200
*/
#define WUXGA_WIDTH	1920
#define WUXGA_HEIGHT	1200
/*
** benchmark timing sample size
*/
#define SAMPLE_SIZE	100
/*
** RGB565
*/
#define WHITE	0xffff
#define YELLOW	0xffe0
#define CYAN	0x07ff
#define GREEN	0x07e0
#define MAGENTA	0xf81f
#define RED	0xf800
#define BLUE	0x001f
#define BLACK	0x0000
#define GRAY	0xc618
/*
** function declarations
*/
uint16_t rgb888_to_rgb565(uint32_t);
uint32_t yuv422_to_rgb888(uint8_t Y, uint8_t U, uint8_t V);
uint16_t yuv422_to_rgb565(uint8_t Y, uint8_t U, uint8_t V);
int display_HDMI(void *fbp, uint8_t *rgb565ptr);
int display_LCD4(void *fbp, void *filebuf);
int convert2(void *cbp, uint8_t *rgb565ptr);
int convert3(void *cbp, uint8_t *rgb565ptr, uint16_t *pbuf);
int RGBColorBars_HDMI(void *fbp);
int RGBColorBars_LCD4(void *fbp);
int RGBDisplayFile_HDMI(void *fbp, char *filepath);
int ReadRGBFile(void *filebuf, char *fpath);
int WriteRGBFile(void *filebuf, char *fpath);
int init_fb_color(void *fbp, uint16_t color);

/*
** haar dwt constants and function declaration
*/
int HaarDwt(uint16_t *imgin_ptr, uint16_t *imgout_ptr);

/*
** general purpose variables
*/
uint8_t *orig_rgb565ptr=NULL, *rgb565ptr=NULL;

/*
** framebuffer variables
*/

int height=0,width=0,step,channels;

int i,j,k;
uint8_t r,g,b, pixel;
int x=0,y=0,idx=0;
uint16_t rgbp, rgb;
long screensize=0;
struct fb_fix_screeninfo finfo;
struct fb_var_screeninfo vinfo;
int fb_fd=0;
uint16_t *fbp=NULL;
void *filebuf=NULL;

/*
** yuv422 to rgb565 look up table (lut) variables
*/
int lut_size = 256*256*256*2;
int lut_fd=0;

/*
** webcam video for linux (v4l2) variables
*/
int vid_fd;
struct v4l2_capability v4l2_cap; 
struct v4l2_format v4l2_fmt;
struct v4l2_requestbuffers v4l2_reqbuf;
struct v4l2_buffer v4l2_buf;
void *cbp = NULL;

/*
** timing declarations
*/
struct timespec fps_start_time, 
		fps_end_time, 
		init_time_start, 
		init_time_end; 
double t_diff=0, fps=0;
int count=SAMPLE_SIZE;
int tlog=1;	/*1=timing on, 0=timing off*/

/***************************************
** main()
***************************************/

int main(void)
{
	if (tlog) clock_gettime(CLOCK_REALTIME, &init_time_start);
	
	/*
	** open the framebuffer
	*/
	
	if((fb_fd = open("/dev/fb0",O_RDWR))<0)
	{
		perror("open\n");
		exit(1);
	}/*eo if*/
	
	/*
	** Initialize the framebuffer
	*/
	ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
	vinfo.grayscale=0;
	vinfo.bits_per_pixel=16;
	if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0){
		printf("FBIOPUT_VSCREENINFO error %d\n", errno);
		exit(1);
	}/*eo if*/

	/*
	** get the framebuffer properties
	*/
	ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);	
	ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);	

	/*
	** calculate the framebuffer screensize
	*/
	screensize = vinfo.yres_virtual * finfo.line_length;

	/*
	** get the address for the framebuffer
	*/
	fbp = NULL;
	fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fbp == MAP_FAILED){
		printf("framebuffer - mmap failed errno %d\n", errno);
		exit(1);
	}/*eo if*/

	/*
	** set up camera
	*/

	/*
	** open webcam
	** video0 = Logitech C920 USB Webcam on Beagle Bone Black
	*/
	if((vid_fd = open("/dev/video0", O_RDWR )) < 0)
	{
		perror("webcam open");
		exit(1);
	}/*eo if*/

	/*
	** get webcam capabilites	
	*/
	if(ioctl(vid_fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0)
	{
		perror("VIDIOC_QUERYCAP");
		exit(1);
	}/*eo if*/

	
	if(!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
	{
		fprintf(stderr, "The device does not handle single-planar video\n");
		exit(1);
	}//eo if

	
	if(!(v4l2_cap.capabilities & V4L2_CAP_STREAMING))
	{
		fprintf(stderr, "The device does not handle frame streaming\n");
		exit(1);
	}//eo if

	/*
	** set the webcam format
	*/
	memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
	v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	v4l2_fmt.fmt.pix.width =  WQVGA_WIDTH;	/*432*/	
	v4l2_fmt.fmt.pix.height = WQVGA_HEIGHT;	/*240*/

	if(ioctl(vid_fd, VIDIOC_S_FMT, &v4l2_fmt) <0)
	{
		perror("VIDIOC_S_FMT");
		exit(1);
	}/*eo if*/

	/*
	** request buffer(s)
	** initiate memory mapped I/O. Memory mapped buffers are located in device
	** memory and must be allocated before they can be mapped into the applications
	** I/O space. 
	*/
	memset(&v4l2_reqbuf, 0, sizeof(v4l2_reqbuf));
	v4l2_reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_reqbuf.memory = V4L2_MEMORY_MMAP;
	v4l2_reqbuf.count = 1;
	
	if(ioctl(vid_fd, VIDIOC_REQBUFS, &v4l2_reqbuf) < 0)
	{
		perror("VIDIOC_REQBUFS");
		exit(0);
	}/*eo if*/

	/*
	** query the status of the buffers (why?)
	*/
	memset(&v4l2_buf, 0, sizeof(v4l2_buf));
	v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_buf.memory = V4L2_MEMORY_MMAP;
	v4l2_buf.index = 0;

	if(ioctl(vid_fd, VIDIOC_QUERYBUF, &v4l2_buf) < 0)
	{
		perror("VIDIOC_QUERYBUF");
		exit(1);
	}/*eo if*/

	/*
	** map webcam buffer to kernel space
	*/
	cbp = mmap(NULL, v4l2_buf.length,PROT_READ | PROT_WRITE,
		   MAP_SHARED, vid_fd, v4l2_buf.m.offset);
		
	if(cbp == MAP_FAILED)
	{
		printf("camera - mmap failed errno=%d\n", errno);
		exit(1);
	}/*eo if*/

	/*
	** start v4l2 streaming
	*/
	memset(&v4l2_buf, 0, sizeof(v4l2_buf));
	v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_buf.memory = V4L2_MEMORY_MMAP;
	v4l2_buf.index = 0;

	int type = v4l2_reqbuf.type;
	int result = ioctl(vid_fd, VIDIOC_STREAMON, &type);	
	if(result < 0)
	{
		perror("VIDIOC_STREAMON");
		int error = errno;
		exit(1);
	}/*eo if*/

	/*
	** set up rgb565file buffer
	*/
	orig_rgb565ptr = rgb565ptr = (uint8_t*)malloc(RGB565_SIZE);
	memset(rgb565ptr, 0, RGB565_SIZE);

	/*
	** use yuv2rgb look up table
	*/
	lut_fd = open("/home/root/yuv2rgb.lut", O_RDWR);
	if(lut_fd < 0){
		printf("yuv2rgb.lut open failed errno=%d\n", errno);
		return(0);
	}/*eo if*/

	/*
	** read yuv2rgb.lut into virtual memory for random access by convert3()
	** changed MAP_SHARED to MAP_RIVATE | MAP_POPULATE for 0.1s per frame performance improvemnt
	*/	
	uint16_t *lut_ptr = mmap(NULL, lut_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, lut_fd, 0);
	if(lut_ptr == MAP_FAILED){
		printf("lut - mmap failed errno=%d\n", errno);
		return(0);
	}/*eo if*/

	/*
	** use madvise() for mmap() performance improvement
	*/
	madvise(lut_ptr, lut_size, MADV_WILLNEED);

	/*
	** read yuv2rgb look up table into memory
	*/
	uint8_t *lut_buf;
	for(i=0; i<lut_size/4096; i++){
		lseek(lut_fd, (long)(i*4096), SEEK_SET);
		read(lut_fd, lut_buf, 1);
	}/*eo for*/
	

	/*
	** clear console and turn off cursor
	** to turn console on use printf("\033[?25h");
	*/
	printf("\033[3J");
	fflush(stdout);
	printf ("\033[?25l");
	fflush(stdout);	

	/*
	** LCD4
	*/
	init_fb_color(fbp, GRAY);
	
	/*
	** allocate output buffer for haar dwt result
	*/	
	uint16_t *imgout_ptr = malloc(sizeof(uint8_t)*432*240*2);
	memset(imgout_ptr, 0xff, 432*240*2);

	/*
	** end of initialization
	*/	
	if(tlog) clock_gettime(CLOCK_REALTIME, &init_time_end);

	/*******************************************************
	********************************************************
	** begin streaming loop
	********************************************************
	*******************************************************/
	if(tlog) clock_gettime(CLOCK_REALTIME, &fps_start_time);
	while(count){

		/*
		** provide camera with a buffer to fill
		*/
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = V4L2_MEMORY_MMAP;
		result = ioctl(vid_fd, VIDIOC_QBUF, &v4l2_buf);
		if( result < 0)
		{
			perror("VIDIOC_QBUF");
			exit(1);
		}/*eo if*/

		/* 
		** the buffer has been filled by the video camera
		** get the buffer for futher processing 
		*/	
		result = ioctl(vid_fd, VIDIOC_DQBUF, &v4l2_buf);
		if( result < 0)
		{
			perror("VIDIOC_DQBUF");
			printf("ERRNO %d\n", errno);
			exit(1);
		}/*eo if*/

		/*
		** convert yuyv422 to rgb565 using look up table
		*/
		convert3(cbp, rgb565ptr, lut_ptr);	

		/*
		** display basic video stream
		*/
		//display_LCD4(fbp, rgb565ptr);
		

		/*
		** process image using haar dwt
		*/
		HaarDwt((uint16_t*)rgb565ptr, imgout_ptr);

		/*
		** display haar dwt video stream
		*/
		display_LCD4(fbp,(uint8_t*)imgout_ptr);
				
		/*
		** timing
		*/
		if(tlog) count--;

	}/*eo while*/

	/*******************************************************
	********************************************************
	** end streaming loop
	********************************************************
	*******************************************************/

	/*
	** benchmark timing
	*/	
	if(tlog){
		clock_gettime(CLOCK_REALTIME, &fps_end_time);
		/*
		** initialization time
		*/
		t_diff = (init_time_end.tv_sec - init_time_start.tv_sec) + 
	        (double)(init_time_end.tv_nsec - init_time_start.tv_nsec)/1000000000.0d;
		t_diff = t_diff/SAMPLE_SIZE;
		printf("initialization time: %f sec\n", t_diff);

		/*
		** frames per second
		*/
		t_diff = (fps_end_time.tv_sec - fps_start_time.tv_sec) + 
	        (double)(fps_end_time.tv_nsec - fps_start_time.tv_nsec)/1000000000.0d;
		t_diff = t_diff/SAMPLE_SIZE;
		fps = (double)1/t_diff;
		printf("%f sec/frame %f frames/sec\n", t_diff, fps);
	}/*eo if*/


	/*
	** deactivate streaming 
	*/
	if (ioctl(vid_fd, VIDIOC_STREAMOFF, &type) < 0)
	{
		perror("VIDIOC_STREAMOFF");
		exit(1);
	}//eo if

	/*
	** clean up
	*/

	/*
	** close webcam
	*/
	close(vid_fd);
	munmap(cbp, v4l2_buf.length);

	/*
	** close framebuffer
	*/
	close(fb_fd);
	munmap(fbp, screensize);
	
	/*
	** yuv422 to rgb565 conversion buffer 
	*/
	free(rgb565ptr);
	if(filebuf) free(filebuf);	

	/*
	** lut 
	*/
	close(lut_fd);
	munmap(lut_ptr,(lut_size));

	printf("done\n");
	return 0;

}/*eo main*****************************/

/*
** display_HDMI
*/
int display_HDMI(void *fbp, uint8_t *rgb565ptr){

	int x=0,y=0,idx=0;
	uint8_t pxlsb=0,pxmsb=0;
	uint8_t *orig_rgb565ptr = rgb565ptr;	/*debug*/
	
	idx = idx+(WUXGA_WIDTH*(HVGA_WIDTH-WQVGA_WIDTH));		
		
	for(y=0; y<WQVGA_HEIGHT; y++){
		for(x=0; x<WQVGA_WIDTH; x++){
			*(uint8_t*)(fbp+idx+(HVGA_HEIGHT-WQVGA_HEIGHT))
				    = *rgb565ptr; 	
			idx++;
			rgb565ptr++;
			*(uint8_t*)(fbp+idx+32) = *rgb565ptr; 
			idx++;
			rgb565ptr++;

		}/*eo for*/
		idx = idx+((WUXGA_WIDTH -WQVGA_WIDTH)*2);
	}/*eo for*/

	return(0);

}/*eo display_HDMI*/

/*
** display the frame buffer on LCD4
*/
int display_LCD4(void *fbp, void *filebuf){


	int i=0,j=0,idx=0;
	uint16_t *fbptr = fbp;
	uint16_t *filebuf_ptr = filebuf;
	uint16_t pixel=0;
		
	idx = HVGA_WIDTH*((HVGA_HEIGHT-WQVGA_HEIGHT)/2);

	for(i=0; i<WQVGA_HEIGHT; i++){		/*row*/

		idx=idx+((HVGA_WIDTH-WQVGA_WIDTH)/2);

		for(j=0; j<WQVGA_WIDTH; j++){	/*col*/

			*(fbptr+idx)=*filebuf_ptr;
			idx++;
			filebuf_ptr++;

		}/*eo for*/

		idx=idx+((HVGA_WIDTH-WQVGA_WIDTH)/2);
	
	}/*eo for*/

	return(0);

}/*eo Display_LCD4*/

/*
** convert2
** uses floating point calculations to convert yuv422 to rgb565
*/
int convert2(void *cbp, uint8_t *rgb565ptr){

	int i=0;
	uint8_t *yuvptr = NULL;
	uint8_t *orig_yuvptr = NULL;
	uint8_t Y0,Y1,U0,V0;
	uint8_t pixel;
	uint16_t rgb565;
	
	orig_yuvptr = yuvptr = (uint8_t *)cbp;

	for(i=0; i<YUYV_SIZE; i++){
		Y1 = *yuvptr;	/*Y1*/
		i++;
		yuvptr++;
		V0 = *yuvptr;	/*V0*/
		i++;
		yuvptr++;
		Y0 = *yuvptr;	/*Y0*/
		i++;
		yuvptr++;
		U0 = *yuvptr;	/*U0*/
		yuvptr++;	/*next 4 byte macropixel*/

		/*
		** convert yuv422 to rgb565
		*/
		rgb565 = yuv422_to_rgb565(Y0,U0,V0);
				
		pixel = rgb565;			
		*rgb565ptr = pixel;
		rgb565ptr++;

		pixel = rgb565 >> 8;		
		*rgb565ptr = pixel;
		rgb565ptr++;

		/*
		** convert yuv422 to rgb565
		*/
		rgb565 = yuv422_to_rgb565(Y1,U0,V0);
				
		pixel = rgb565;			
		*rgb565ptr = pixel;
		rgb565ptr++;

		pixel = rgb565 >> 8;		
		*rgb565ptr = pixel;
		rgb565ptr++;

	}/*eo for*/
	
	return(0);

}/*eo convert2*/

/*
** convert3
** uses yuv2rgb.lut look up table to convert yuv422 to rgb565
*/
int convert3(void *cbp, uint8_t *rgb565ptr, uint16_t *pbuf){

	int i=0;
	uint8_t *yuvptr = NULL;
	uint8_t Y0,Y1,U0,V0;
	uint8_t pixel;
	uint16_t rgb565=0xffff;
		
	yuvptr = (uint8_t *)cbp;

	for(i=0; i<YUYV_SIZE; i++){
		Y1 = *yuvptr;	/*Y1*/
		i++;
		yuvptr++;
		V0 = *yuvptr;	/*V0*/
		i++;
		yuvptr++;
		Y0 = *yuvptr;	/*Y0*/
		i++;
		yuvptr++;
		U0 = *yuvptr;	/*U0*/
		yuvptr++;	/*next 4 byte macropixel*/

		/*
		** convert yuv422 to rgb565
		*/
		rgb565 = *(pbuf + ((Y0*256*256)+(U0*256)+V0));
				
		pixel = rgb565;			
		*rgb565ptr = pixel;
		rgb565ptr++;

		pixel = rgb565 >> 8;		
		*rgb565ptr = pixel;
		rgb565ptr++;
		

		/*
		** convert yuv422 to rgb565
		*/
		rgb565 = *(pbuf + ((Y1*256*256)+(U0*256)+V0));
		
		pixel = rgb565;			
		*rgb565ptr = pixel;
		rgb565ptr++;

		pixel = rgb565 >> 8;		
		*rgb565ptr = pixel;
		rgb565ptr++;

	}/*eo for*/
	
	return(0);

}/*eo convert3*/

/*
** yuv422 to rgb888 conversion
*/
uint32_t yuv422_to_rgb888(uint8_t Y, uint8_t U, uint8_t V){

	uint32_t  rgb_red=0, rgb_blue=0, rgb_green=0;
	uint32_t rgb888=0;

	/*
	** ITU-R 708
	*/

	/*
	float r = ( (1.164*(float)(Y-16)) + (2.115*(float)(V-128)) );
	if(r<0) r=0;
	if(r>255) r=255;
	rgb_red = (uint32_t)r;

	float g = ( (1.164*(float)(Y-16)) - (0.534*(float)(V-128)) - 
                    (0.213*(float)(U-128)) );
	if(g<0) g=0;
	if(g>255) g=255;
	rgb_green = (uint32_t)g;

	float b = ( (1.164*(float)(Y-16)) + (1.793*(float)(U-128)) );
	if(b<0) b=0;
	if(b>255) b=255;
	rgb_blue = (uint32_t)b;
	*/

	/*
	** ITU-R 601
	*/
	float r = 1.164*(float)(Y-16) + 1.596*(float)(V-128);
	if(r<0) r=0;
	if(r>255) r=255;
	rgb_red = (uint32_t)r;

	float g = 1.164*(float)(Y-16) - 0.813*(float)(V-128) - 0.391*(float)(U-128);
	if(g<0) g=0;
	if(g>255) g=255;
	rgb_green = (uint32_t)g;
	
	float b = 1.164*(float)(Y-16) + 2.018*(float)(U-128);
	if(b<0) b=0;
	if(b>255) b=255;
	rgb_blue = (uint32_t)b;
		
	/*
	** create packed rgb888
	*/
	rgb_red = rgb_red << 8;
	rgb_green = rgb_green << 16;
	rgb_blue = rgb_blue << 24;
	rgb888 = rgb888 | rgb_red | rgb_green | rgb_blue;

	return(rgb888);

}/*eo yuv422_to_rgb888*/

/*
** convert yuv422 to rgb565
*/
uint16_t yuv422_to_rgb565(uint8_t Y, uint8_t U, uint8_t V){

	float r,g,b;
	uint8_t red = 0, green=0, blue=0;
	uint16_t r16=0, g16=0, b16=0, rgb565=0, bgr565=0;

	/*
	** ITU-R 601
	*/
	r = 1.164*(float)(Y-16) + 1.596*(float)(V-128);
	if(r<0) r=0;
	if(r>255) r=255;
	red = (uint8_t)r;

	g = 1.164*(float)(Y-16) - 0.813*(float)(V-128) - 0.391*(float)(U-128);
	if(g<0) g=0;
	if(g>255) g=255;
	green = (uint8_t)g;
	
	b = 1.164*(float)(Y-16) + 2.018*(float)(U-128);
	if(b<0) b=0;
	if(b>255) b=255;
	blue = (uint8_t)b;

	/*
	** rgb565 format used in x86
	*/
	/*
	r16 = ((red >>3) & 0x1f) << 11;	
	g16 = ((green >> 2) & 0x3f) << 5;
	b16 = (blue >> 3) & 0x1f;
	rgb565 = r16 | g16 | b16;

	return(rgb565);
	*/
	
	/*
	** bgr565 format used in ARM & Beaglebone Black
	*/
	
	b16 = ((blue >>3) & 0x1f) << 11;	
	g16 = ((green >> 2) & 0x3f) << 5;
	r16 = (red >> 3) & 0x1f;
	bgr565 = b16 | g16 | r16;

	return(bgr565);
	

}/*eo yuv422_to_rgb565*/

/*
** convert rgb888 to rgb565
*/
uint16_t rgb888_to_rgb565(uint32_t rgb888){
	
	uint8_t red = 0, green=0, blue=0;
	uint16_t r=0, g=0, b=0, rgb565=0;

	/* uint32_t rgb888 format
	** msb 24-31 red, 16-23 green, 8-15 blue, lsb 0-7 0x00
	*/

	red = rgb888 >> 24;
	green = rgb888 >> 16;
	blue = rgb888 >> 8;

	/*
	red = rgb888 >> 8;
	green = rgb888 >> 16;
	blue = rgb888 >> 24;
	*/

	/* uint16_t rgb565 format
	** msb 11-15 red, 5-10 green, 0-4 blue
	*/
	r = ((red >>3) & 0x1f) << 11;
	g = ((green >> 2) & 0x3f) << 5;
	b = (blue >> 3) & 0x1f;

	rgb565 = r | g | b;

	return(rgb565);

}/*eo rgb888_to_rgb565*/

/*
** display RGB Color Bars on BeagleBone HDMI
*/
int RGBColorBars_HDMI(void *fbp){

	int x=0,y=0,idx=0;
  	uint8_t pxlsb=0,pxmsb=0;
		
	idx = idx+(WUXGA_WIDTH*(HVGA_WIDTH-WQVGA_WIDTH));		
		
	for(y=0; y<WQVGA_HEIGHT; y++){
		for(x=0; x<WQVGA_WIDTH; x++){
			if(x>= 0 && x<= 53){
				pxmsb = 0xff;	/*white*/
				pxlsb = 0xff;
			}/*eo if*/
			if(x>= 54 && x<= 107){
				pxmsb = 0xff;	/*yellow*/
				pxlsb = 0xe0;
			}/*eo if*/
			if(x>= 108 && x<= 161){
				pxmsb = 0x07;	/*cyan*/
				pxlsb = 0xff;
			}/*eo if*/
			if(x>= 162 && x<= 215){
				pxmsb = 0x07;	/*green*/
				pxlsb = 0xe0;
			}/*eo if*/
			if(x>= 216 && x<= 269){
				pxmsb = 0xf8;	/*magenta*/
				pxlsb = 0x1f;
			}/*eo if*/
			if(x>= 270 && x<= 323){				
				pxmsb = 0xf8;	/*red*/
				pxlsb = 0x00;
			}/*eo if*/
			if(x>= 324 && x<= 377){
				pxmsb = 0x00;	/*blue*/
				pxlsb = 0x1f;
			}/*eo if*/
			if(x>= 378 && x<= 431){
				pxmsb = 0x00;	/*black*/
				pxlsb = 0x00;
			}/*eo if*/
			*(uint8_t*)(fbp+idx+(HVGA_HEIGHT-WQVGA_HEIGHT))
				    = pxlsb; /*pixel lsb*/	
			idx++;
			*(uint8_t*)(fbp+idx+32) = pxmsb; /*pixel msb*/
			idx++;
		}/*eo for*/
		idx = idx+((WUXGA_WIDTH -WQVGA_WIDTH)*2);
	}/*eo for*/

	return(0);

}/*eo RGBColorBars_HDMI*/

/*
** display color bars on the BeagleBone LCD4
*/		
int RGBColorBars_LCD4(void *fbp){


	int i=0,j=0,idx=0;
	uint16_t *fbptr = fbp;
	uint16_t pixel=0;
	
	idx = HVGA_WIDTH*((HVGA_HEIGHT-WQVGA_HEIGHT)/2);

	for(i=0; i<WQVGA_HEIGHT; i++){		/*row*/

		idx=idx+((HVGA_WIDTH-WQVGA_WIDTH)/2);

		for(j=0; j<WQVGA_WIDTH; j++){	/*col*/

			if(j>=   0 && j<=  53) pixel = WHITE;
			if(j>=  54 && j<= 107) pixel = YELLOW;
			if(j>= 108 && j<= 161) pixel = CYAN;
			if(j>= 162 && j<= 215) pixel = GREEN;
			if(j>= 216 && j<= 269) pixel = MAGENTA;
			if(j>= 270 && j<= 323) pixel = RED;
			if(j>= 324 && j<= 377) pixel = BLUE;
			if(j>= 378 && j<= 431) pixel = BLACK;
			
			*(fbptr+idx)=pixel;
			idx++;

		}/*eo for*/

		idx=idx+((HVGA_WIDTH-WQVGA_WIDTH)/2);
	
	}/*eo for*/

	return(0);

}/*eo RGBColorBars_LCD4*/	


/*
** opens and display raw rgb565 files using HDMI
*/
int RGBDisplayFile_HDMI(void *fbp, char *filepath){

	int x=0,y=0,idx=0;
  	uint8_t pxlsb=0,pxmsb=0;

	FILE *fp=NULL;
	int errnum=0, fsize=0;
	struct stat filestat;
	uint8_t *fbuf=NULL, *orig_fbuf=NULL;

	fp = fopen(filepath, "r");
	if(fp == NULL){
		errnum = errno;
		printf("error opening file: %s\n", strerror(errnum));
		return (1);
	}/*eo if*/
	fstat(fileno(fp), &filestat);
	fsize = filestat.st_size;
	orig_fbuf = fbuf = (uint8_t*)malloc(fsize);
	memset(fbuf, 0, fsize);
	fread(fbuf,sizeof(uint8_t),fsize,fp);
	fclose(fp);
	
	idx = idx+(WUXGA_WIDTH*(HVGA_WIDTH-WQVGA_WIDTH));		
		
	for(y=0; y<WQVGA_HEIGHT; y++){
		for(x=0; x<WQVGA_WIDTH; x++){
			pxlsb = *fbuf;
			fbuf++;
			pxmsb = *fbuf;
			fbuf++;
			*(uint8_t*)(fbp+idx+(HVGA_HEIGHT-WQVGA_HEIGHT))
				    = pxlsb; /*pixel lsb*/	
			idx++;
			*(uint8_t*)(fbp+idx+32) = pxmsb; /*pixel msb*/
			idx++;
		}/*eo for*/
		idx = idx+((WUXGA_WIDTH -WQVGA_WIDTH)*2);
	}/*eo for*/
	free(fbuf);
	return(0);

}/*eo RGBDisplayFile_HDMI*/


/*
** Read a file into a buffer for display
** file needs to be: 
** WQVGA_WIDTH	432 
** WQVGA_HEIGHT	240
** FILEBUF_SIZE	WQVGA_WIDTH*WQVGA_HEIGHT*2
*/
int ReadRGBFile(void *filebuf, char *fpath){

	FILE *fd=NULL;
	int fsize=0;

	/*
	** read file into a buffer
	*/
	fd = fopen(fpath, "r");
	if(fd == NULL){
		printf("error %d opening file %s\n", errno, fpath);
		return(-1);
	}/*eo if*/
	fseek(fd, 0, SEEK_END);
	fsize = ftell(fd);
	rewind(fd);
	fread(filebuf,sizeof(uint8_t),fsize,fd);
	fclose(fd);

	return(0);

}/*eo ReadRGBFile*/
	
/*
** initialize frame buffer color
*/
int init_fb_color(void *fbp, uint16_t color){

	uint16_t *fbptr=fbp;
	int i=0;

	/*
	** initialize frame buffer color
	*/
	fbptr=fbp;
	for(i=0; i<HVGA_WIDTH*HVGA_HEIGHT; i++){
		*fbptr = color;
		fbptr++;
	}/*eo for*/

	return(0);

}/*eo init_fb_color*/

/*
** write rgb565 buffer to a file
*/
int WriteRGBFile(void *filebuf, char *fpath){

	FILE *fd=NULL;
	
	/*
	** write buffer to file
	*/
	fd = fopen(fpath, "w");
	fwrite(filebuf,sizeof(uint8_t),RGB565_SIZE,fd);
	fclose(fd);
	
	return(0);

}/*eo WriteRGBFile*/

/*
** HaarDwt
** Transform rgb565 image to haar dwt rgb565 image
**
** input variables:
** uint16_t *imgin_ptr is the input buffer
** uint16_t *imgout_ptr is the output buffer
*/
int HaarDwt(uint16_t *imgin_ptr, uint16_t *imgout_ptr)
{

	#define QUAD_ROW_ORIGIN	432/2*240 	
	#define QUAD_COL_OFFSET 432/2		
	#define QUAD_ROW_OFFSET 432		
	#define H_NUM_ROWS	240
	#define H_NUM_COLS	432
	#define H_ROW_WIDTH	432

	/*
	** input buffer indicies
	*/
	int i=0,j=0;

	/*
	** output buffer indicies
	*/	
	int k=0,h=0;	
	int quad_row_offset=0;

	/*
	** haar dwt sliding window r1c1,r1c2,r2c1,r2c2
	** contains 2 byte rgb565 pixel
	*/
	int r1c1=0,r1c2=0,r2c1=0,r2c2=0;

	/*
	** haar dwt r,g,b sliding window pixels
	** the 2 byte rgb565 pixel is broken into r,g,b
	** pixels for seperate r,g,b channel processing
	*/
	int red_r1c1=0,red_r1c2=0,red_r2c1=0,red_r2c2=0;
	int grn_r1c1=0,grn_r1c2=0,grn_r2c1=0,grn_r2c2=0;
	int blu_r1c1=0,blu_r1c2=0,blu_r2c1=0,blu_r2c2=0;

	/*
	** haar dwt low pass (lp), high pass (hp) results
	** used to calculate ll,lh,hl,hh results
	*/
	int lp1=0,lp2=0,lp3=0;
	int hp1=0,hp2=0,hp3=0;

	/*
	** store r,g,b pixels by haar dwt ll,lh,hl,hh results
	*/
	uint8_t red_ll=0, red_lh=0, red_hl=0, red_hh=0;
	uint8_t grn_ll=0, grn_lh=0, grn_hl=0, grn_hh=0;
	uint8_t blu_ll=0, blu_lh=0, blu_hl=0, blu_hh=0;

	/*
	** combine seperate r,g,b ll,lh,hl,hh pixels into single 2 byte
	** rgb565 ll,lh,hl,hh pixels
	*/
	uint16_t rgb565_ll=0, rgb565_lh=0, rgb565_hl=0, rgb565_hh=0;

	/*
	** haar dwt
	** this haar dwt uses a sliding window r1c1,r1c2,r2c1,r2c2
	** to traverse the entire rgb565 image.
	**
	** H_ROW_WIDTH*i points to the first row (r1)
	** H_ROW_WIDTH*(i+1) points to the second row (r2)
	** index j points to the first column (c1) in the row (r1,r2)
	** index j+1 points to the second column (c2) in the row (r1,r2)
	*/

	/*
	** two rows at a time until all rows processed
	*/	
	for(i=0; i<H_NUM_ROWS; i++){	
		
		/*
		** calculate the output buffer row offset (quad_row_offset)
		** and initialize output buffer column index (k) before 
		** processing the rows and columns
		*/
		quad_row_offset = QUAD_ROW_OFFSET*h;	
		k=0;	

		/*
		** two columns at a time until all columns processed
		*/
		for(j=0; j<H_NUM_COLS; j++){	

			/*
			** sliding window
			** get rgb565 pixels r1c1,r1c2,r2c1,r2c2
			*/
			r1c1 = *(imgin_ptr+(((H_ROW_WIDTH*i)+j)));		//r1c1
			r1c2 = *(imgin_ptr+(((H_ROW_WIDTH*i)+(j+1))));		//r1c2
			r2c1 = *(imgin_ptr+(((H_ROW_WIDTH*(i+1))+j)));		//r2c1
			r2c2 = *(imgin_ptr+(((H_ROW_WIDTH*(i+1))+(j+1))));	//r2c2
			
			/*
			** input buffer
			** point to beginning of next two columns
			*/
			j++;	

			/*
			** unpack rgb565 pixels into red, green, blue
			*/
			red_r1c1 = (((r1c1 & 0xf800)>>3)>>8);
			grn_r1c1 = ((r1c1 & 0x07e0)>>5);
			blu_r1c1 = (r1c1 & 0x001f);
			
			red_r1c2 = (((r1c2 & 0xf800)>>3)>>8);
			grn_r1c2 = ((r1c2 & 0x07e0)>>5);
			blu_r1c2 = (r1c2 & 0x001f);

			red_r2c1 = (((r2c1 & 0xf800)>>3)>>8);
			grn_r2c1 = ((r2c1 & 0x07e0)>>5);
			blu_r2c1 = (r2c1 & 0x001f);
			
			red_r2c2 = (((r2c2 & 0xf800)>>3)>>8);
			grn_r2c2 = ((r2c2 & 0x07e0)>>5);
			blu_r2c2 = (r2c2 & 0x001f);

			/*
			** calculate red ll,lh,hl,hh pixels
			*/
			lp1 = (red_r1c1+red_r2c1)/2; if(lp1>0x1f) lp1=0x1f;
			lp2 = (red_r1c2+red_r2c2)/2; if(lp2>0x1f) lp2=0x1f;
			lp3 = (lp1+lp2)/2; if(lp3>0x1f) lp3=0x1f;
			red_ll = lp3;

			hp1 = abs((lp1-lp2)/2); if(hp1>0x1f) hp1=0x1f;
			red_lh = hp1*10;

			hp1 = abs((red_r1c1-red_r2c1)/2); if(hp1>0x1f) hp1=0x1f;
			hp2 = abs((red_r1c2-red_r2c2)/2); if(hp2>0x1f) hp2=0x1f;
			lp1 = (hp1+hp2)/2; if(lp1>0x1f) lp1=0x1f;
			red_hl = lp1*10;

			hp3 = abs((hp1-hp2)/2); if(hp3>0x1f) hp3=0x1f;
			red_hh = hp3*10;
			
			/*
			** calculate green ll,lh,hl,hh pixels
			*/
			lp1 = (grn_r1c1+grn_r2c1)/2; if(lp1>0x3f) lp1=0x3f;
			lp2 = (grn_r1c2+grn_r2c2)/2; if(lp2>0x3f) lp2=0x3f;
			lp3 = (lp1+lp2)/2; if(lp3>0x3f) lp3=0x3f;
			grn_ll = lp3;

			hp1 = abs((lp1-lp2)/2); if(hp1>0x3f) hp1=0x3f;
			grn_lh = hp1*10;

			hp1 = abs((grn_r1c1-grn_r2c1)/2); if(hp1>0x3f) hp1=0x3f;
			hp2 = abs((grn_r1c2-grn_r2c2)/2); if(hp2>0x3f) hp2=0x3f;
			lp1 = (hp1+hp2)/2; if(lp1>0x3f) lp1=0x3f;
			grn_hl = lp1*10;

			hp3 = abs((hp1-hp2)/2); if(hp3>0x3f) hp3=0x3f;
			grn_hh = hp3*10;

			/*
			** calculate blue ll,lh,hl,hh pixels
			*/
			lp1 = (blu_r1c1+blu_r2c1)/2; if(lp1>0x1f) lp1=0x1f;
			lp2 = (blu_r1c2+blu_r2c2)/2; if(lp2>0x1f) lp2=0x1f;
			lp3 = (lp1+lp2)/2; if(lp3>0x1f) lp3=0x1f;
			blu_ll = lp3;

			hp1 = abs((lp1-lp2)/2); if(hp1>0x1f) hp1=0x1f;
			blu_lh = hp1*10;

			hp1 = abs((blu_r1c1-blu_r2c1)/2); if(hp1>0x1f) hp1=0x1f;
			hp2 = abs((blu_r1c2-blu_r2c2)/2); if(hp2>0x1f) hp2=0x1f;
			lp1 = (hp1+hp2)/2; if(lp1>0x1f) lp1=0x1f;
			blu_hl = lp1*10;

			hp3 = abs((hp1-hp2)/2); if(hp3>0x1f) hp3=0x1f;
			blu_hh = hp3*10;

			/*
			** pack into ll,lh,hl,hh rgb565 pixels
			*/
			rgb565_ll = ((red_ll << 11) | (grn_ll << 5) | (blu_ll));
			rgb565_lh = ((red_lh << 11) | (grn_lh << 5) | (blu_lh));
			rgb565_hl = ((red_hl << 11) | (grn_hl << 5) | (blu_hl));
			rgb565_hh = ((red_hh << 11) | (grn_hh << 5) | (blu_hh));

			/*
			** put rgb565 pixel into ll,lh,hl,hh quadrant in
			** output buffer
			*/

			/*ll*/
			*(imgout_ptr+(quad_row_offset+k))=rgb565_ll;
			/*lh*/	
			*(imgout_ptr+(QUAD_COL_OFFSET+(quad_row_offset+k)))=rgb565_lh;
			/*hl*/
			*(imgout_ptr+((QUAD_ROW_ORIGIN)+(quad_row_offset+k)))=rgb565_hl;
			/*hh*/
			*(imgout_ptr+((QUAD_ROW_ORIGIN)+QUAD_COL_OFFSET+(quad_row_offset+k)))=rgb565_hh;			
			
			/*
			** output buffer
			** point to next column
			*/
			k++;	
			
		}/*eo for*/
		
		/*
		** input buffer
		** point to beginning of next two rows
		*/
		i++;

		/*
		** output buffer
		** point to next row
		*/	
		h++;	
		
	}/*eo for*/

	return(0);

}/*eo HaarDwt*/



