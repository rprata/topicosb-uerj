#include <cuda.h>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>

#include "rf-time.h"

using namespace std;

#ifdef __cplusplus
 #define __STDC_CONSTANT_MACROS
 #ifdef _STDINT_H
  #undef _STDINT_H
 #endif
 # include <stdint.h>
#endif

#define CUDA_SAFE_CALL 
#define ELEM(i,j,DIMX_) ((i)+(j)*(DIMX_))

#define NUM_BLUR 50

extern "C" {

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
#include <libavutil/mathematics.h>

#include <SDL.h>
#include <SDL_thread.h>

AVFormatContext * pFormatCtx;
AVCodecContext * pCodecCtx, * c = NULL;
AVCodec * pCodec;
AVFrame * pFrameRGB, * pDecodedFrame, * pOutputFrame;
AVPacket packet;
struct SwsContext * sws_ctx = NULL, * out_sws_ctx = NULL;

int video_stream = -1;

SDL_Overlay * bmp;
SDL_Rect rect;
SDL_Event event;

const char * filename;
uint8_t * bufferRGB, * bufferYUV;
uint8_t * outbuf;
int numBytesRGB, numBytesYUV;
int frameFinished;
int counter_frames = 0;

int outbuf_size = 300000, out_size;

FILE * pFile;

int blSizeX = 16, blSizeY = 16;

__host__ int setup_video(const char * filename);
__host__ SDL_Overlay * init_sdl_window(AVCodecContext * pCodecCtx, SDL_Overlay * bmp);
__host__ void play_original_video(const char * arg);
__host__ void filter_video(AVFrame * pFrame, int width, int height);
__global__ void grayGPU(unsigned char *image, int width, int height);
__global__ void blurGPU(unsigned char * image, int width, int height); 


__host__ int main (int argc, char ** argv)
{
	if (argc != 2)
	{
		fprintf(stderr, "Para rodar o programa, use: %s [video_path]\n", argv[0]);
		return -1;
	}

	if (setup_video(argv[1]) < 0)
		return -1;

	while(av_read_frame(pFormatCtx, &packet) >= 0)
	{
	  	//Testa se e unm pacote com de stream de video
	  	if(packet.stream_index == video_stream) 
	  	{	  		
			// Decode frame de video
		    avcodec_decode_video2(pCodecCtx, pDecodedFrame, &frameFinished, &packet);
		    
		    //Testa se ja existe um quadro de video
		    if (frameFinished) 
		    {
		    	SDL_LockYUVOverlay(bmp);

		   		//Converte a imagem de seu formato nativo para RGB
		   		sws_scale
				(
					sws_ctx,
					(uint8_t const * const *) pDecodedFrame->data,
					pDecodedFrame->linesize,
					0,
					pCodecCtx->height,
					pFrameRGB->data,
					pFrameRGB->linesize
				);

				filter_video(pFrameRGB, pCodecCtx->width, pCodecCtx->height);

				//Convertendo de RFB para YUV
				sws_scale (
					out_sws_ctx, 
					(uint8_t const * const *) pFrameRGB->data, 
					pFrameRGB->linesize, 
        			0, 
        			c->height, 
        			pOutputFrame->data, 
        			pOutputFrame->linesize
        		);	

	    		pOutputFrame->data[0] = bmp->pixels[0];
				pOutputFrame->data[1] = bmp->pixels[2];
				pOutputFrame->data[2] = bmp->pixels[1];

				pOutputFrame->linesize[0] = bmp->pitches[0];
				pOutputFrame->linesize[1] = bmp->pitches[2];
				pOutputFrame->linesize[2] = bmp->pitches[1];

				SDL_UnlockYUVOverlay(bmp);

				rect.x = 0;
				rect.y = 0;
				rect.w = 1280;
				rect.h = 720;

				SDL_DisplayYUVOverlay(bmp, &rect);


				#ifdef SAVE_VIDEO
				//codigo para salvar frames em uma saida
				fflush(stdout);
				out_size = avcodec_encode_video(c, outbuf, outbuf_size, pOutputFrame);
				std::cout << "write frame " << counter_frames << "(size = " << out_size << ")" << std::endl;
				fwrite(outbuf, 1, out_size, pFile);
				#endif
				
				counter_frames++;

		     }	
    	}
    
  		// Libera o pacote alocado pelo pacote
  		av_free_packet(&packet);
  		
  		SDL_PollEvent(&event);
	    
	    switch(event.type) 
	    {
	    	case SDL_QUIT: SDL_Quit();
	    		return 0;
	      		break;
	    	default:
	      		break;
	    }
	}

	//captura frames atrasados 
    for(; out_size; counter_frames++) { 
        fflush(stdout); 
                
        out_size = avcodec_encode_video(c, outbuf, outbuf_size, NULL); 
		std::cout << "write frame " << counter_frames << "(size = " << out_size << ")" << std::endl;
        fwrite(outbuf, 1, outbuf_size, pFile);       
    } 

	// adiciona sequencia para um real mpeg
    outbuf[0] = 0x00;
    outbuf[1] = 0x00;
    outbuf[2] = 0x01;
    outbuf[3] = 0xb7;

	fwrite(outbuf, 1, 4, pFile);

	fclose(pFile);
	free(outbuf);

	av_free(bufferRGB);
	av_free(bufferYUV);

	//Fecha o codec
	avcodec_close(pCodecCtx);
	avcodec_close(c);

	//Fecha o arquivo de video
	avformat_close_input(&pFormatCtx);

	return 0;
}

__host__ int setup_video(const char * filename) 
{
	//Registra todos os codecs e formatos de videos
	av_register_all();

	//Abre o arquivo de midia;	
	if (avformat_open_input(&pFormatCtx, filename, NULL, NULL)!=0) 
	{
		fprintf(stderr, "Nao foi possivel abrir o arquivo %s\n", filename);
		return -1;
	}

	//Recupera a informacao do stream;
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
  	{
  		fprintf(stderr, "Nao foi possivel encontrar a informacao do stream\n");
  		return -1; 
  	}

  	//Informacao bruta sobre o arquivo de video;
	av_dump_format(pFormatCtx, 0, filename, 0);

	//Encontra o primeiro stream de video (video principal)	
	for (unsigned i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			video_stream = i;
			break;
		}
	}

	if (video_stream == -1)
	{
		fprintf(stderr, "Nao foi possivel encontrar o stream de video\n");
		return -1;
	}

	//Captura o ponteiro referente ao codec do stream de video
	pCodecCtx = pFormatCtx->streams[video_stream]->codec;

	//Busca o decode do video
	if ((pCodec = avcodec_find_decoder(pCodecCtx->codec_id)) == NULL)
	{
		fprintf(stderr, "Codec nao suportado :(\n");
		return -1;
	}

	//Abre o codec	
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		fprintf(stderr, "Nao foi possivel abrir o codec\n");
	}

	// Aloca espaco de memoria para o frame de video (AVFrame)
	pDecodedFrame = avcodec_alloc_frame();

	if ((pFrameRGB = avcodec_alloc_frame()) == NULL)
  	{
  		fprintf(stderr, "Nao foi possivel alocar memoria para o frame de video\n");
	  	return -1;
  	}

  	if ((pOutputFrame = avcodec_alloc_frame()) == NULL)
  	{
  		fprintf(stderr, "Nao foi possivel alocar memoria para o frame de video\n");
	  	return -1;
  	}
	
	//Determina o tamanho necessario do buffer e aloca a memoria
	numBytesRGB = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
	
	bufferRGB = (uint8_t *) av_malloc(numBytesRGB*sizeof(uint8_t));

	//Configura o contexto para o escalonamento
	sws_ctx = sws_getContext (
	        pCodecCtx->width,
	        pCodecCtx->height,
	        pCodecCtx->pix_fmt,
	        pCodecCtx->width,
	        pCodecCtx->height,
	        PIX_FMT_RGB24,
	        SWS_BILINEAR,
	        NULL,
	        NULL,
	        NULL
	);

	//Aplica para o buffer os frames no formato FMT_RGB24 (pacote RGB 8:8:8, 24bpp, RGBRGB...)
	avpicture_fill((AVPicture *) pFrameRGB, bufferRGB , PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

	//Preparando AVCodecContext de saida
	AVCodec * codec = avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);

	if (!codec)
	{
		fprintf(stderr, "Codec nao encontrado\n");
		return -1;
	}

	c = avcodec_alloc_context3(codec);

	//Configurando valores para o contexto do video de saida
    c->bit_rate = pCodecCtx->bit_rate;
    c->width = pCodecCtx->width;
    c->height = pCodecCtx->height;
    c->time_base = pCodecCtx->time_base;
    c->gop_size = pCodecCtx->gop_size;
    c->max_b_frames = pCodecCtx->max_b_frames;
    c->pix_fmt = PIX_FMT_YUV420P;

    if (avcodec_open2(c, codec, NULL) < 0) return -1;

    pFile = fopen("out.mpg", "wb");
	if (!pFile) 
	{
    	fprintf(stderr, "could not open out.mpg\n");
	    return -1;
	}

	outbuf = (uint8_t *) av_malloc(outbuf_size);


	//Criacao de contexto para converter um tipo RGB24 para YUV240P (preparacao para encoded)
    numBytesYUV = avpicture_get_size(PIX_FMT_YUV420P, c->width, c->height);
	
	bufferYUV = (uint8_t *) av_malloc(numBytesYUV*sizeof(uint8_t));

    out_sws_ctx = sws_getContext (
	        c->width,
	        c->height,
	        PIX_FMT_RGB24,
	       	c->width,
	        c->height,
	        PIX_FMT_YUV420P,
	        SWS_FAST_BILINEAR,
	        NULL,
	        NULL,
	        NULL
	);

	avpicture_fill((AVPicture *) pOutputFrame, bufferYUV , PIX_FMT_YUV420P, c->width, c->height);
	
	bmp = init_sdl_window(pCodecCtx, bmp);
	
	if (bmp == NULL) 
	{
		return -1;
	}
	
	// play_original_video(filename);
}

__host__ SDL_Overlay * init_sdl_window(AVCodecContext * pCodecCtx, SDL_Overlay * bmp) 
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) 
	{
    	fprintf(stderr, "Nao foi possivel inicializar o SDL - %s\n", SDL_GetError());
    	return NULL;
  	}

  	SDL_Surface * screen;

	screen = SDL_SetVideoMode(1280, 720, 0, 0);
	if (!screen) 
	{
  		fprintf(stderr, "SDL: Nao foi possivel configurar o modo do video\n");
  		return NULL;
	}

	bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height, SDL_YV12_OVERLAY, screen);
	
  	return bmp;
}

__host__ void play_original_video(const char * arg) 
{
	char command[50];
	sprintf(command, "vlc %s &",arg);
	system(command);
}

__host__ void filter_video(AVFrame * pFrame, int h_width, int h_height)
{
	int  size = 3 * h_height * h_width;

	unsigned char * d_image = NULL;
	CUDA_SAFE_CALL(cudaHostAlloc((void**)&d_image, size, cudaHostAllocWriteCombined|cudaHostAllocMapped));

	// Calcula dimensoes da grid e dos blocos
	dim3 blockSize( blSizeX, blSizeY );
	int numBlocosX = h_width  / blockSize.x + ( h_width  % blockSize.x == 0 ? 0 : 1 );
	int numBlocosY = h_height / blockSize.y + ( h_height % blockSize.y == 0 ? 0 : 1 );
	dim3 gridSize( numBlocosX, numBlocosY, 1 );

	CUDA_SAFE_CALL(cudaMemcpy(d_image, pFrame->data[0], size, cudaMemcpyHostToDevice));
	grayGPU<<< gridSize, blockSize >>>(d_image, h_width, h_height);
	for (int i = 0; i < NUM_BLUR; i++)
		blurGPU<<< gridSize, blockSize >>>(d_image, h_width, h_height);
	CUDA_SAFE_CALL(cudaMemcpy(pFrame->data[0], d_image, size, cudaMemcpyDeviceToHost));
	
	CUDA_SAFE_CALL(cudaFreeHost(d_image));
}

__global__ void grayGPU(unsigned char * image, int width, int height) 
{

	int i = threadIdx.x + blockIdx.x*blockDim.x;
	int j = threadIdx.y + blockIdx.y*blockDim.y;

	if(i < width && j < height) {

	 	int idx = 3*ELEM(i, j, width);
	 	int r = image[ idx+2 ];
	 	int g = image[ idx+1 ];
	 	int b = image[ idx   ];
		float gg = (float)r*0.299f +(float)g*0.587f + (float)b*0.114f;
	 	int gray = (int)gg;
	 	image[ idx   ] = (unsigned char)gray;
	 	image[ idx+1 ] = (unsigned char)gray;
	 	image[ idx+2 ] = (unsigned char)gray;		
	 }
}

__global__ void blurGPU(unsigned char * image, int width, int height) 
{

	int i = threadIdx.x + blockIdx.x*blockDim.x;
	int j = threadIdx.y + blockIdx.y*blockDim.y;

	if ((i < (width - 1)) && (j < (height - 1))) 
	{
	 	int idx = 3*ELEM(i, j, width);
	 	int center = image[ idx ];
	 	idx = 3*ELEM(i - 1, j, width);
	 	int left = image[ idx ];
	 	idx = 3*ELEM(i + 1, j, width);
	 	int right = image[ idx ];
	 	idx = 3*ELEM(i, j + 1, width);
	 	int top = image[ idx ];
	 	idx = 3*ELEM(i, j - 1, width);
	 	int bottom = image[ idx ];

	 	idx = 3*ELEM(i, j, width);

		float gg = (float) ((float) center + (float) left + (float) right + (float) top + (float) bottom)/5.0f;
	 	int blur = (int)gg;
	 	image[ idx   ] = (unsigned char)blur;
	 	image[ idx+1 ] = (unsigned char)blur;
	 	image[ idx+2 ] = (unsigned char)blur;		
	 }
}

}
