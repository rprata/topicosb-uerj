#include <cstdio>
#include <iostream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cmath>

#include "rf-time.h"

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
#include <libavutil/mathematics.h>

#include <SDL.h>
#include <SDL_thread.h>


#define NUMBER_SAVED_FRAMES 250
// #define SAVE_VIDEO
#define SDL_INTERFACE

AVFormatContext * pFormatCtx;
AVCodecContext * pCodecCtx;
AVCodec * pCodec;
AVFrame * pFrameRGB, * pDecodedFrame, * pOutputFrame;
AVPacket packet;
struct SwsContext * sws_ctx = NULL, * out_sws_ctx = NULL;

SDL_Overlay * bmp;
SDL_Rect rect;
SDL_Event event;

const char * filename;
uint8_t * bufferRGB, * bufferYUV;
int numBytesRGB, numBytesYUV;
int frameFinished;
int counter_frames = 0;

string filter_type;
int numBlur = 1;
int isComplex = 1;

ofstream logfile;
double initial_time, final_time, elapsedTime, totalTime;
char buff[100];

void filter_video(AVFrame * pFrame, int width, int height);
void filter_average(AVFrame * pFrame, int width, int height);
void gray_filter(uint8_t * bufferRGB);
void blur_filterSimplex(uint8_t * center, uint8_t * left, uint8_t * right, uint8_t * top, uint8_t * bottom);
void blur_filterComplex(uint8_t * center, uint8_t * left, uint8_t * right, uint8_t * top, uint8_t * bottom);
void save_frame(AVFrame * pFrame, int width, int height, int iFrame);
void play_original_video(const char * arg);
SDL_Overlay * init_sdl_window(AVCodecContext * pCodecCtx, SDL_Overlay * bmp);
int main(int argc, char ** argv)
{
	if(	argc == 1)
	{
		fprintf(stderr, "Para rodar o programa, use: %s  <video> <interacoes> <filter-type>\n", argv[0]);
		return -1;
	}
	switch(argc) {

	case 3:
		numBlur = atoi( argv[2] );
		break;
	case 4:
		numBlur = atoi( argv[2] );
		filter_type = string(argv[3]);
		break;
	}

	fprintf( stderr, "Numero de filtragens: %d\n", numBlur );
	fprintf(stderr, "Tipo do filtro: %s\n", filter_type.c_str() );

	if (filter_type == "simplex")
	{
		isComplex = 0;
	}
	else if (filter_type == "complex")
	{
		isComplex = 1;
	}
	else
	{
		fprintf(stderr, "Filtro não existe \n");
		return 1;
	}
	sprintf(buff, "%s%d", "log", numBlur);

	logfile.open(buff, ofstream::out | ofstream::app);

	filename = argv[1];

	//Registra todos os codecs e formatos de videos
	av_register_all();
	avdevice_register_all();

	AVInputFormat *iformat = av_find_input_format("video4linux2");

	if (!iformat)
	{
		fprintf(stderr, "Nao foi possivel abrir o dispostivo\n");
		return -1;	
	}

	//Abre o arquivo de midia;	
	if (avformat_open_input(&pFormatCtx, filename, iformat, NULL)!=0) 
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
	int video_stream = -1;
	
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
	AVCodecContext * c = NULL;
	AVCodec * codec = avcodec_find_encoder(pFormatCtx->streams[video_stream]->codec->codec_id);

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

#ifdef SAVE_VIDEO
    FILE * pFile = fopen("out.mpg", "wb");
	if (!pFile) 
	{
    	fprintf(stderr, "could not open out.mpg\n");
	    return -1;
	}
#endif

	int outbuf_size = 300000, out_size;
	uint8_t * outbuf = (uint8_t *) av_malloc(outbuf_size);


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

#ifdef SDL_INTERFACE	
	bmp = init_sdl_window(pCodecCtx, bmp);
	
	if (bmp == NULL) 
	{
		return -1;
	}
	
#endif

	while(av_read_frame(pFormatCtx, &packet) >= 0 || 1) 
	{
	  	//Testa se e unm pacote com de stream de video
	  	if(packet.stream_index == video_stream) 
	  	{	  		
			// Decode frame de video
		    avcodec_decode_video2(pCodecCtx, pDecodedFrame, &frameFinished, &packet);
		    
		    //Testa se ja existe um quadro de video
		    if (frameFinished) 
		    {
		    	#ifdef SDL_INTERFACE
		    	SDL_LockYUVOverlay(bmp);
		    	#endif



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

				// initial_time = get_clock_msec();
				// for (int i = 0; i < numBlur; i++)
				// 	filter_average(pFrameRGB, pCodecCtx->width, pCodecCtx->height);

				elapsedTime = get_clock_msec() - initial_time;
				logfile << elapsedTime << endl;
				totalTime += elapsedTime;

				#if defined(SDL_INTERFACE) || defined(SAVE_VIDEO)

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
				#endif

				#ifdef SDL_INTERFACE
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
				#endif

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
#ifdef SDL_INTERFACE
  		SDL_PollEvent(&event);
	    
	    switch(event.type) 
	    {
	    	case SDL_QUIT: SDL_Quit();
	    		return 0;
	      		break;
	    	default:
	      		break;
	    }
#endif	    
	}
#ifdef SAVE_VIDEO
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
#endif

	logfile << "Tempo Total: " << totalTime << endl;
	
	//Fecha o codec
	avcodec_close(pCodecCtx);

	//Fecha o arquivo de video
	avformat_close_input(&pFormatCtx);

	return 0;

}

void play_original_video(const char * arg) 
{
	char command[50];
	sprintf(command, "vlc %s &",arg);
	system(command);
}

SDL_Overlay * init_sdl_window(AVCodecContext * pCodecCtx, SDL_Overlay * bmp) 
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

//Funcao que salva o frame em uma imagem ppm (posteriormente manipular imagem)
void save_frame(AVFrame * pFrame, int width, int height, int iFrame) 
{
	FILE *pFile;
	char szFilename[32];
	int  y, k;

	// Abre arquivo
	sprintf(szFilename, "../images/frame%d.ppm", iFrame);
	pFile=fopen(szFilename, "wb");
	if(pFile==NULL)
	return;

	// Escreve cabeçalho
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Escreve os pixels em um arquivo ppm
	for(y = 0; y < height; y++)
	{
		for (k = 0; k < 3 * width; k += 3)
			//Aplicando meu filtro de tons de cinza :P
			gray_filter((pFrame->data[0] + y*pFrame->linesize[0]) + k);

		fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
	}

	// Fecha arquivo
	fclose(pFile);
}

void filter_video(AVFrame * pFrame, int width, int height)
{
	int  y, k;
	//Aplicando meu filtro de tons de cinza :P
	for(y = 0; y < height; y++)
		for (k = 0; k < 3 * width; k += 3)
		{
			gray_filter((pFrame->data[0] + y*pFrame->linesize[0] + k));
		}
}

void gray_filter(uint8_t * bufferRGB)
{
	float r, g, b;
	uint8_t out;

	b = (float)(0.114f * (int) *(bufferRGB));
	g = (float)(0.587f * (int) *(bufferRGB + 1));
	r = (float)(0.299f * (int) *(bufferRGB + 2));
	out = (uint8_t) (r + g + b);
	*(bufferRGB) = out;
	*(bufferRGB + 1) = out;
	*(bufferRGB + 2) = out;
}

void filter_average(AVFrame * pFrame, int width, int height)
{
	int y, k;
	// Aplicando filtro dos borroes
	for(y = 1; y < height - 1; y++)
	{
		for(k = 3; k < 3 * (width - 1); k += 3)
		{
			uint8_t * center = (pFrame->data[0] + y*pFrame->linesize[0] + k);
			uint8_t * left = (pFrame->data[0] + y*pFrame->linesize[0] + k - 3);
			uint8_t * right = (pFrame->data[0] + y*pFrame->linesize[0] + k + 3);
			uint8_t * top =  (pFrame->data[0] + (y - 1)*pFrame->linesize[0] + k);
			uint8_t * bottom = (pFrame->data[0] + (y + 1)*pFrame->linesize[0] + k);

			if (isComplex)
				blur_filterComplex(center, left, right, top, bottom);
			else
				blur_filterSimplex(center, left, right, top, bottom);
		}
	}
}

void blur_filterSimplex(uint8_t * center, uint8_t * left, uint8_t * right, uint8_t * top, uint8_t * bottom)
{
	float pixel;
	uint8_t out;
	pixel = (float) ((float) ((int) *(center)) + (float) ((int) *(left)) + (float) ((int) *(right)) + (float) ((int) *(top)) + (float) ((int) *(bottom)))/5.0f;
	out = (uint8_t) pixel;
	*(center) = out;

	pixel = (float) ((float) ((int) *(center + 1)) + (float) ((int) *(left + 1)) + (float) ((int) *(right + 1)) + (float) ((int) *(top + 1)) + (float) ((int) *(bottom + 1) + 1))/5.0f;
	out = (uint8_t) pixel;
	*(center + 1) = out;

	pixel = (float) ((float) ((int) *(center + 2)) + (float) ((int) *(left + 2)) + (float) ((int) *(right + 2)) + (float) ((int) *(top + 2)) + (float) ((int) *(bottom + 1) + 2))/5.0f;
	out = (uint8_t) pixel;
	*(center + 2) = out;
}


void blur_filterComplex(uint8_t * center, uint8_t * left, uint8_t * right, uint8_t * top, uint8_t * bottom)
{
	float pixel;
	uint8_t out;
	float k1, k2, k3, k4;

	k1 = sqrt((float)((*center - *left)*(*center - *left)));
	k2 = sqrt((float)((*center - *right)*(*center - *right)));
	k3 = sqrt((float)((*center - *top)*(*center - *top)));
	k4 = sqrt((float)((*center - *bottom)*(*center - *bottom)));


	pixel = (float) ((float) ((int) *(center)) + (float) ((int) *(left)*k1) + 
		(float) ((int) *(right)*k2) + (float) ((int) *(top)*k3) + 
		(float) ((int) *(bottom)*k4))/(float)(1 + k1 + k2 + k3 +k4);
	out = (uint8_t) pixel;
	*(center) = out;

	k1 = sqrt((float)((*(center + 1) - *(left + 1))*(*(center + 1) - *(left + 1))));
	k2 = sqrt((float)((*(center + 1) - *(right + 1))*(*(center + 1) - *(right + 1))));
	k3 = sqrt((float)((*(center + 1) - *(top + 1))*(*(center + 1) - *(top + 1))));
	k4 = sqrt((float)((*(center + 1) - *(bottom + 1))*(*(center + 1) - *(bottom + 1))));

	pixel = (float) ((float) ((int) *(center + 1)) + (float) ((int) *(left + 1)*k1) + 
		(float) ((int) *(right + 1)*k2) + (float) ((int) *(top + 1)*k3) + 
		(float) ((int) *(bottom + 1)*k4 + 1))/(float)(1 + k1 + k2 + k3 +k4);
	out = (uint8_t) pixel;
	*(center + 1) = out;

	k1 = sqrt((float)((*(center + 2) - *(left + 2))*(*(center + 2) - *(left + 1))));
	k2 = sqrt((float)((*(center + 2) - *(right + 2))*(*(center + 2) - *(right + 1))));
	k3 = sqrt((float)((*(center + 2) - *(top + 2))*(*(center + 2) - *(top + 1))));
	k4 = sqrt((float)((*(center + 2) - *(bottom + 2))*(*(center + 2) - *(bottom + 1))));

	pixel = (float) ((float) ((int) *(center + 2)) + (float) ((int) *(left + 2)*k1) + 
		(float) ((int) *(right + 2)*k2) + (float) ((int) *(top + 2)*k3) + 
		(float) ((int) *(bottom + 2)*k4 + 1))/(float)(1 + k1 + k2 + k3 +k4);	
	out = (uint8_t) pixel;
	*(center + 2) = out;
}

}