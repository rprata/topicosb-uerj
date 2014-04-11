#include <cstdio>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#define NUMBER_SAVED_FRAMES 50

AVFormatContext * pFormatCtx;
AVCodecContext * pCodecCtx;
AVCodec * pCodec;
AVFrame * pFrameRGB, * pDecodedFrame;
AVPacket packet;
struct SwsContext * sws_ctx = NULL;

const char * filename;
uint8_t * buffer;
int numBytes;
int frameFinished;
long long counter_frames = 0;

void save_frame(AVFrame * pFrame, int width, int height, int iFrame);

int main(int argc, char ** argv)
{
	if (argc != 2)
	{
		fprintf(stderr, "Para rodar o programa, use: %s [video_path]\n", argv[0]);
		return -1;
	}

	filename = argv[1];

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
	
	//Determina o tamanho necessario do buffer e aloca a memoria
	numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
	
	buffer = (uint8_t *) av_malloc(numBytes*sizeof(uint8_t));

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
	avpicture_fill((AVPicture *) pFrameRGB, buffer , PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

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
				
				if (counter_frames > NUMBER_SAVED_FRAMES) 
					break;

				save_frame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, counter_frames);				
				
				counter_frames++;
		     }	
    	}
    
  		// Libera o pacote alocado pelo pacote
  		av_free_packet(&packet);
	}

	//Libera os Frames RGB
	av_free(buffer);
	av_free(pFrameRGB);

	//Fecha o codec
	avcodec_close(pCodecCtx);

	//Fecha o arquivo de video
	avformat_close_input(&pFormatCtx);

	return 0;

}

//Funcao que salva o frame em um ppm (posteriormente manipular imagem)
void save_frame(AVFrame * pFrame, int width, int height, int iFrame) 
{
	FILE *pFile;
	char szFilename[32];
	int  y;

	// Open file
	sprintf(szFilename, "frame%d.ppm", iFrame);
	pFile=fopen(szFilename, "wb");
	if(pFile==NULL)
	return;

	// Write header
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Write pixel data
	for(y=0; y<height; y++)
	fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

	// Close file
	fclose(pFile);
}

}