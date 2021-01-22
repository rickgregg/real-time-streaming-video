/*
** haar4.c
** perform haar dwt on rgb565 image - optimized
**
** richard l. gregg
** march 20, 2018
*/

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUAD_ROW_ORIGIN 	256*512
#define QUAD_COL_OFFSET 	256
#define QUAD_ROW_OFFSET 	512

#define H_ROW_WIDTH		512
#define H_COL_HEIGHT		512

int HaarDwt(uint16_t *imgin_ptr, uint16_t *imgout_ptr);

int main(void){

	FILE *fd=NULL;
	int fsize=0;
	uint16_t *imgin_ptr=NULL, *imgout_ptr=NULL;

	/*
	** read lena_rgb565.raw file into a buffer
	*/
	fd = fopen("/home/rgregg/Desktop/lena_rgb565.raw", "r");
	if(fd == NULL){
		printf("error %d opening file lena_rgb565.raw", errno);
		return(-1);
	}/*eo if*/
	fseek(fd, 0, SEEK_END);
	fsize = ftell(fd);
	rewind(fd);
	imgin_ptr = malloc(sizeof(uint8_t)*fsize);
	fread(imgin_ptr,sizeof(uint8_t),fsize,fd);
	fclose(fd);

	/*
	** allocate output buffer for haar dwt result
	*/	
	imgout_ptr = malloc(sizeof(uint8_t)*512*512*2);
	memset(imgout_ptr, 0xff, 512*512*2);

	/*
	** perform haar dwt on rgb565 image
	*/
	HaarDwt(imgin_ptr, imgout_ptr);
	
	/*
	** write haar dwt output buffer to a file
	*/
	fd = fopen("/home/rgregg/Desktop/lena_haar_rgb565_opt.raw", "w");
	if(fd == NULL){
		printf("error %d writing file lena_haar_rgb565_opt.raw", errno);
		return(-1);
	}/*eo if*/
	fwrite(imgout_ptr, sizeof(uint8_t), sizeof(uint8_t)*512*512*2, fd);
	fclose(fd); 
	
	/*
	** clean up
	*/
	free(imgin_ptr);
	free(imgout_ptr);
	
	printf("done\n");
	return(0);

}/*eo main*/

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
	for(i=0; i<H_ROW_WIDTH; i++){	
		
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
		for(j=0; j<H_COL_HEIGHT; j++){	
			
			/*
			** sliding window
			** get rgb565 pixels r1c1,r1c2,r2c1,r2c2
			*/
			r1c1 = *(imgin_ptr+((H_ROW_WIDTH*i)+j));		/*r1c1*/
			r1c2 = *(imgin_ptr+((H_ROW_WIDTH*i)+(j+1)));		/*r1c2*/
			r2c1 = *(imgin_ptr+((H_ROW_WIDTH*(i+1))+j));		/*r2c1*/
			r2c2 = *(imgin_ptr+((H_ROW_WIDTH*(i+1))+(j+1)));	/*r2c2*/
			
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


