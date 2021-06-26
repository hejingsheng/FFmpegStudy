#include <stdio.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

void saveYuv(FILE *fp, AVFrame* avFrame)
{
	uint32_t pitchY = avFrame->linesize[0];
	uint32_t pitchU = avFrame->linesize[1];
	uint32_t pitchV = avFrame->linesize[2];

	uint8_t* avY = avFrame->data[0];
	uint8_t* avU = avFrame->data[1];
	uint8_t* avV = avFrame->data[2];
	uint32_t i = 0;
	for (i = 0; i < avFrame->height; i++) {
		fwrite(avY, avFrame->width, 1, fp);
		avY += pitchY;
	}

	for (i = 0; i < avFrame->height / 2; i++) {
		fwrite(avU, avFrame->width / 2, 1, fp);
		avU += pitchU;
	}

	for (i = 0; i < avFrame->height / 2; i++) {
		fwrite(avV, avFrame->width / 2, 1, fp);
		avV += pitchV;
	}
}

void savePcm(FILE *fp, AVFrame* avFrame)
{
	if (avFrame->channels == 2)
	{
		uint32_t len = avFrame->nb_samples * av_get_bytes_per_sample(avFrame->format);
		int i = 0;
		while (i < len)
		{
			fwrite(avFrame->data[0] + i, 1, 4, fp);
			fwrite(avFrame->data[1] + i, 1, 4, fp);
			i += 4;
		}
	}
	else if (avFrame->channels == 1)
	{
		uint8_t* data = avFrame->data[0];
		uint32_t len = avFrame->nb_samples * av_get_bytes_per_sample(avFrame->format);
		fwrite(avFrame->data[0], 1, len, fp);
	}
	else
	{
		av_log(NULL, AV_LOG_WARNING, "not support\n");
	}
}

int decode_frame(FILE *fp, AVCodecContext *codec, AVPacket *pkt, int type)
{
	int ret;
	AVFrame *frame = NULL;

	ret = avcodec_send_packet(codec, pkt);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "send packet fail");
		return -1;
	}
	frame = av_frame_alloc();
	if (frame == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "alloc frame error");
		return -1;
	}
	while (ret >= 0)
	{
		ret = avcodec_receive_frame(codec, frame);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
			{
				av_log(NULL, AV_LOG_WARNING, "send packet fail");
				av_frame_free(&frame);
				return 0;
			}
			av_log(NULL, AV_LOG_ERROR, "decode fail %s", av_err2str(ret));
			av_frame_free(&frame);
			return ret;
		}
		if (type == 0)
		{
			saveYuv(fp, frame);
		}
		else
		{
			savePcm(fp, frame);
		}
		av_frame_unref(frame);
	}
	av_frame_free(&frame);
	return 0;
}

int demuxing_decode()
{
	int ret;
	AVFormatContext *ifmt_ctx = NULL;
	AVPacket *pkt = NULL;

	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat *ofmt = NULL;

	AVCodecContext *video_dec_ctx = NULL;
	AVCodec *video_dec = NULL;

	AVCodecContext *audio_dec_ctx = NULL;
	AVCodec *audio_dec = NULL;

	char *filename = "test.mp4";
	char *yuvfilename = "test.yuv";
	char *pcmfilename = "test.pcm";
	char errors[1024];
	int video_stream_id = -1;
	int audio_stream_id = -1;
	FILE *fp_yuv;
	FILE *fp_pcm;

	int stream_map_num;
	int *stream_map;
	int stream_index = 0;
	int i;

	fp_yuv = fopen(yuvfilename, "wb");
	if (fp_yuv == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "open yuv fail");
		goto ERROR;
	}
	fp_pcm = fopen(pcmfilename, "wb");
	if (fp_pcm == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "open pcm fail");
		goto ERROR;
	}

	ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL);
	if (ret < 0)
	{
		av_strerror(ret, errors, 1024);
		av_log(NULL, AV_LOG_DEBUG, "Could not open source file: %s, %d(%s)\n",
			filename,
			ret,
			errors);
		goto ERROR;
	}

	av_dump_format(ifmt_ctx, 0, filename, 0);

	video_stream_id = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream_id < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "can not find video stream");
		goto ERROR;
	}
	audio_stream_id = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audio_stream_id < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "can not find audio stream");
		goto ERROR;
	}

	ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, "test.flv");
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open out format fail");
		goto ERROR;
	}
	stream_map_num = ifmt_ctx->nb_streams;
	stream_map = av_mallocz_array(stream_map_num, sizeof(*stream_map));
	if (stream_map == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "out of memery");
		goto ERROR;
	}
	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < ifmt_ctx->nb_streams; i++)
	{
		AVStream *outStream = NULL;
		AVStream *inStream = ifmt_ctx->streams[i];
		if (inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
			inStream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
			inStream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
		{
			stream_map[i] = -1;
			continue;
		}
		stream_map[i] = stream_index++;
		outStream = avformat_new_stream(ofmt_ctx, NULL);
		avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
		outStream->codecpar->codec_tag = 0;
	}
	av_dump_format(ofmt_ctx, 0, "test.flv", 1);
	ret = avio_open2(&ofmt_ctx->pb, "test.flv", AVIO_FLAG_WRITE, NULL, NULL);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open output fail");
		goto ERROR;
	}


	video_dec = avcodec_find_decoder(ifmt_ctx->streams[video_stream_id]->codecpar->codec_id);
	if (video_dec == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "not support codec");
		goto ERROR;
	}
	video_dec_ctx = avcodec_alloc_context3(video_dec);
	if (video_dec_ctx == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "alloc dec codec fail");
		goto ERROR;
	}
	avcodec_parameters_to_context(video_dec_ctx, ifmt_ctx->streams[video_stream_id]->codecpar);

	if ((ret = avcodec_open2(video_dec_ctx, video_dec, NULL)) < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open codec fail");
		goto ERROR;
	}

	audio_dec = avcodec_find_decoder(ifmt_ctx->streams[audio_stream_id]->codecpar->codec_id);
	if (audio_dec == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "not support codec");
		goto ERROR;
	}
	audio_dec_ctx = avcodec_alloc_context3(audio_dec);
	if (audio_dec_ctx == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "alloc dec codec fail");
		goto ERROR;
	}
	avcodec_parameters_to_context(audio_dec_ctx, ifmt_ctx->streams[audio_stream_id]->codecpar);

	if ((ret = avcodec_open2(audio_dec_ctx, audio_dec, NULL)) < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open codec fail");
		goto ERROR;
	}

	pkt = av_packet_alloc();
	if (pkt == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "can not alloc packet");
		goto ERROR;
	}
	av_init_packet(pkt);
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "write header fail");
		goto ERROR;
	}
	while ((ret = av_read_frame(ifmt_ctx, pkt) >= 0))
	{
		AVStream *in_stream, *out_stream;

		if (pkt->stream_index == video_stream_id)
		{
			av_log(NULL, AV_LOG_INFO, "find video stream");
			if (decode_frame(fp_yuv, video_dec_ctx, pkt, 0) < 0)
			{
				av_log(NULL, AV_LOG_ERROR, "decode error");
				av_packet_unref(pkt);
				break;
			}
		}
		else if (pkt->stream_index == audio_stream_id)
		{
			av_log(NULL, AV_LOG_INFO, "find audio stream");
			if (decode_frame(fp_pcm, audio_dec_ctx, pkt, 1) < 0)
			{
				av_log(NULL, AV_LOG_ERROR, "decode error");
				av_packet_unref(pkt);
				break;
			}
		}
		in_stream = ifmt_ctx->streams[pkt->stream_index];
		if (pkt->stream_index >= stream_map_num ||
			stream_map[pkt->stream_index] < 0)
		{
			av_packet_unref(pkt);
			continue;
		}
		pkt->stream_index = stream_map[pkt->stream_index];
		out_stream = ofmt_ctx->streams[pkt->stream_index];
		pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
		pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
		pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
		pkt->pos = -1;
		av_interleaved_write_frame(ofmt_ctx, pkt);

		av_packet_unref(pkt);
	}
	av_write_trailer(ofmt_ctx);

ERROR:
	if (ofmt_ctx)
	{
		avio_closep(&ofmt_ctx->pb);
		avformat_free_context(ofmt_ctx);
		av_freep(&stream_map);
	}
	if (ifmt_ctx)
	{
		avformat_close_input(&ifmt_ctx);
	}
	if (pkt)
	{
		av_packet_free(&pkt);
	}
	if (video_dec_ctx)
	{
		avcodec_free_context(&video_dec_ctx);
	}
	if (audio_dec_ctx)
	{
		avcodec_free_context(&audio_dec_ctx);
	}
	if (fp_yuv)
	{
		fclose(fp_yuv);
	}
	if (fp_pcm)
	{
		fclose(fp_pcm);
	}

	return 0;
}

#define ADTS_HEADER_LEN  7;

static int get_audio_obj_type(int aactype) {
	//AAC HE V2 = AAC LC + SBR + PS
	//AAV HE = AAC LC + SBR
	//所以无论是 AAC_HEv2 还是 AAC_HE 都是 AAC_LC
	switch (aactype) {
	case 0:
	case 2:
	case 3:
		return aactype + 1;
	case 1:
	case 4:
	case 28:
		return 2;
	default:
		return 2;

	}
}

static int get_sample_rate_index(int samples, int aactype) {

	int i = 0;
	int freq_arr[13] = {
		96000, 88200, 64000, 48000, 44100, 32000,
		24000, 22050, 16000, 12000, 11025, 8000, 7350
	};

	//如果是 AAC HEv2 或 AAC HE, 则频率减半
	if (aactype == 28 || aactype == 4) {
		samples /= 2;
	}

	for (i = 0; i < 13; i++) {
		if (samples == freq_arr[i]) {
			return i;
		}
	}
	return 4;//默认是44100
}

static int get_channel_config(int channels, int aactype) {
	//如果是 AAC HEv2 通道数减半
	if (aactype == 28) {
		return (channels / 2);
	}
	return channels;
}

int general_adts_header(char *adtsHeader, int dataLen, int aactype, int samples, int channels)
{
	int audio_object_type = get_audio_obj_type(aactype);
	int sampling_frequency_index = get_sample_rate_index(samples, aactype);
	int channel_config = get_channel_config(channels, aactype);


	int adtsLen = dataLen + 7;

	adtsHeader[0] = 0xff;         //syncword:0xfff                          高8bits
	adtsHeader[1] = 0xf0;         //syncword:0xfff                          低4bits
	adtsHeader[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
	adtsHeader[1] |= (0 << 1);    //Layer:0                                 2bits 
	adtsHeader[1] |= 1;           //protection absent:1                     1bit

	adtsHeader[2] = (audio_object_type - 1) << 6;            //profile:audio_object_type - 1                      2bits
	adtsHeader[2] |= (sampling_frequency_index & 0x0f) << 2; //sampling frequency index:sampling_frequency_index  4bits 
	adtsHeader[2] |= (0 << 1);                             //private bit:0                                      1bit
	adtsHeader[2] |= (channel_config & 0x04) >> 2;           //channel configuration:channel_config               高1bit

	adtsHeader[3] = (channel_config & 0x03) << 6;     //channel configuration:channel_config      低2bits
	adtsHeader[3] |= (0 << 5);                      //original：0                               1bit
	adtsHeader[3] |= (0 << 4);                      //home：0                                   1bit
	adtsHeader[3] |= (0 << 3);                      //copyright id bit：0                       1bit  
	adtsHeader[3] |= (0 << 2);                      //copyright id start：0                     1bit
	adtsHeader[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits

	adtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
	adtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);       //frame length:value    低3bits
	adtsHeader[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
	adtsHeader[6] = 0xfc;

	return 0;
}

int write_aac_stream(FILE *fp, int aacType, int channels, int samples, AVPacket *pkt)
{
	char adtsHeander[7] = { 0 };

	general_adts_header(adtsHeander, pkt->size, aacType, samples, channels);
	fwrite(adtsHeander, 1, 7, fp);
	fwrite(pkt->data, 1, pkt->size, fp);
	return 0;
}

int write_spspps_data(FILE *fp, const uint8_t *codec_extradata, const int codec_extradata_size)
{
	uint8_t *data = codec_extradata;
	uint8_t spsNums;
	uint8_t ppsNums;
	uint32_t spsLen = 0;
	uint32_t ppsLen = 0;
	char startCode[4] = { 0x00,0x00,0x00,0x01 };
	char *spspps = NULL;

	spsNums = data[5] & 0x1f;
	spsLen = data[6] << 8 | data[7];
	data += 8;
	while (spsNums > 0)
	{
		fwrite(startCode, 1, 4, fp);
		fwrite(data, 1, spsLen, fp);
		data += spsLen;
		spsNums--;
	}

	ppsNums = *data++;
	ppsLen = *data++ << 8 | *data++;
	while (ppsNums > 0)
	{
		fwrite(startCode, 1, 4, fp);
		if (ppsLen > 0)
		{
			fwrite(data, 1, ppsLen, fp);
		}
		data += ppsLen;
		ppsNums--;
	}

	return 0;
}

int write_frame_data(FILE *fp, const uint8_t* data, const uint32_t len)
{
	char startCode[4] = { 0x00,0x00,0x00,0x01 };

	fwrite(startCode, 1, 4, fp);
	fwrite(data, 1, len, fp);
	return 0;
}

int write_h264_stream(FILE *fp, AVFormatContext *fmt_ctx, AVPacket *pkt)
{
	uint8_t *data;
	uint32_t len;
	uint32_t nalLen;
	uint8_t naluHeader;
	uint8_t nalType;
	uint32_t currentLen = 0;

	data = pkt->data;
	len = pkt->size;

	if (len < 5)
	{
		av_log(NULL, AV_LOG_ERROR, "data error\n");
		return -1;
	}

	do
	{
		nalLen = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
		naluHeader = data[4];
		nalType = naluHeader & 0x1f;
		if (nalType == 5)
		{
			write_spspps_data(fp, fmt_ctx->streams[pkt->stream_index]->codecpar->extradata, fmt_ctx->streams[pkt->stream_index]->codecpar->extradata_size);
			av_log(NULL, AV_LOG_INFO, "this is key frame\n");
			write_frame_data(fp, data + 4, nalLen);
		}
		else
		{
			av_log(NULL, AV_LOG_INFO, "this is not key frame\n");
			write_frame_data(fp, data + 4, nalLen);
		}
		data += (nalLen + 4);
		currentLen += (nalLen + 4);
	} while (currentLen < len);
	return 0;
}

int get_h264_aac_stream()
{
	int ret;
	AVFormatContext *ifmt_ctx = NULL;
	AVPacket *pkt = NULL;

	AVStream *videoStream = NULL;
	AVStream *audioStream = NULL;

	char *filename = "123.mp4";
	char *h264filename = "123.h264";
	char *aacfilename = "123.aac";
	char errors[1024];
	int video_stream_id = -1;
	int audio_stream_id = -1;
	int aac_type;
	int channels;
	int samples;
	FILE *fp_h264;
	FILE *fp_aac;

	fp_h264 = fopen(h264filename, "wb");
	if (fp_h264 == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "open yuv fail\n");
		goto ERROR;
	}
	fp_aac = fopen(aacfilename, "wb");
	if (fp_aac == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "open yuv fail\n");
		goto ERROR;
	}

	ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open file error\n");
		goto ERROR;
	}

	av_dump_format(ifmt_ctx, 0, filename, 0);

	video_stream_id = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream_id < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "can not find video stream\n");
		goto ERROR;
	}
	videoStream = ifmt_ctx->streams[video_stream_id];
	if (videoStream->codecpar->codec_id != AV_CODEC_ID_H264)
	{
		av_log(NULL, AV_LOG_ERROR, "video is not h264 stream\n");
		goto ERROR;
	}

	audio_stream_id = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audio_stream_id < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "can not find audio stream\n");
		goto ERROR;
	}
	audioStream = ifmt_ctx->streams[audio_stream_id];
	if (audioStream->codecpar->codec_id != AV_CODEC_ID_AAC)
	{
		av_log(NULL, AV_LOG_ERROR, "audio is not aac stream\n");
		goto ERROR;
	}
	aac_type = audioStream->codecpar->profile;
	channels = audioStream->codecpar->channels;
	samples = audioStream->codecpar->sample_rate;
	
	pkt = av_packet_alloc();
	if (pkt == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "can not alloc packet\n");
		goto ERROR;
	}
	av_init_packet(pkt);
	while ((ret = av_read_frame(ifmt_ctx, pkt)) >= 0)
	{
		if (pkt->stream_index == video_stream_id)
		{
			av_log(NULL, AV_LOG_INFO, "find video stream\n");
			write_h264_stream(fp_h264, ifmt_ctx, pkt);
		}
		else if (pkt->stream_index == audio_stream_id)
		{
			av_log(NULL, AV_LOG_INFO, "find audio stream\n");
			write_aac_stream(fp_aac, aac_type, channels, samples, pkt);
		}
		else
		{

		}
		av_packet_unref(pkt);
	}

ERROR:
	if (ifmt_ctx)
	{
		avformat_close_input(&ifmt_ctx);
	}
	if (pkt)
	{
		av_packet_free(&pkt);
	}
	if (fp_h264)
	{
		fclose(fp_h264);
	}
	if (fp_aac)
	{
		fclose(fp_aac);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	printf("hello from ffmpegTest!\n");
	av_log_set_level(AV_LOG_DEBUG);

	//get_h264_aac_stream();
	demuxing_decode();
	return 0;
}