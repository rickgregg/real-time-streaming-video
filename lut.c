/*
** lut.c
** create yuv422 to rgb565 look up table
** write table to disk file
**
** february 10, 2018 - rlg
*/

#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define Y_SIZE 256
#define U_SIZE 256
#define V_SIZE 256

uint16_t yuv422_to_rgb565(uint8_t Y, uint8_t U, uint8_t V);

int main(void){
	uint8_t y=0,u=0,v=0;
	int i,j,k;
	long count=0;
	uint16_t pixel;
	int err=0;
	int fd=0;
	int size = 256*256*256*2;
	
	fd = open("/home/rgregg/Desktop/yuv2rgb.lut", O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
	if(fd < 0){
		err = errno;
		printf("open failed errno=%d\n", errno);
		return(0);
	}/*eo if*/

	if( lseek(fd, size-1, SEEK_SET) < 0){
		err = errno;
		printf("lseek failed errno=%d\n", errno);
		return(0);
	}/*eo if*/

	if( write(fd, "", 1) < 0){
		err = errno;
		printf("lseek failed errno=%d\n", errno);
		return(0);
	}/*eo if*/
	
	uint16_t *pbuf = mmap(NULL, size, PROT_WRITE, MAP_SHARED,fd,0);
	if(pbuf == MAP_FAILED){
		err = errno;
		printf("mmap failed errno=%d\n", errno);
		return(0);
	}/*eo if*/

	
	/*
	** create yuv2rgb look up table
	*/
	uint16_t *pb = pbuf;		 	
	for(i=0; i<Y_SIZE; i++){
		y = (uint8_t)i;
		for(j=0; j<U_SIZE; j++){
			u = (uint8_t)j;
			for(k=0; k<V_SIZE; k++){
				v = (uint8_t)k;
				count++;
				pixel  = yuv422_to_rgb565(y,u,v);
				printf("%ld Y=%d U=%d V=%d RGB=%04x\n",count,y,u,v,pixel);
				*pb = pixel;
				pb++;
			}/*eo for*/
			
		}/*eo for*/
		
	}/*eo for*/

	/*
	** write yuv2rgb look up table to disk
	*/
	msync(pbuf, size, MS_SYNC);

	/*
	** clean up
	*/
	close(fd);
	munmap(pbuf,(size));
	printf("done\n");

	return(0);

}/*eo main*/

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
	*/
	
	/*
	** bgr565 format used in ARM & Beaglebone Black
	*/
	b16 = ((blue >>3) & 0x1f) << 11;	
	g16 = ((green >> 2) & 0x3f) << 5;
	r16 = (red >> 3) & 0x1f;
	bgr565 = b16 | g16 | r16;

	return(bgr565);

}/*eo yuv422_to_rgb888*/







