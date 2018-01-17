#include "mp4Writer.h"

#include <stdio.h>
#include <string>
#include <vector>
#include <list>
#include <iostream>
#include "TS-adjust.h"

using namespace std;

FILE *fp_open_v;
FILE *fp_open_a;

mp4Writer::mp4Writer()
{
	_outputCfg.ofmt = NULL;
	_inputCfg.ifmt_ctx = NULL;
	_outputCfg.ofmt_ctx = NULL;

	_inputCfg.indexVideo = -1;
	_inputCfg.indexAudio = -1;
	_outputCfg.indexVideo = -1;
	_outputCfg.indexAudio = -1;

	_buffer = new unsigned char[2*1024 * 1024];
}


mp4Writer::~mp4Writer()
{
	delete _buffer;
}

static int fill_iobuffer_v(void *opaque, uint8_t *buf, int buf_size)
{
	if (!feof(fp_open_v)){
		int true_size = fread(buf, 1, buf_size, fp_open_v);
		return true_size;
	}
	else{
		return -1;
	}
}

static int fill_iobuffer_a(void *opaque, uint8_t *buf, int buf_size)
{
	if (!feof(fp_open_a)){
		int true_size = fread(buf, 1, buf_size, fp_open_a);
		return true_size;
	}
	else{
		return -1;
	}
}

int mp4Writer::open_input_file(InputCfg_S *p, FILE * &pFile, char *url)
{
	int ret;

	p->ifmt_ctx = avformat_alloc_context();

	if ((ret = avformat_open_input(&p->ifmt_ctx, p->filename.c_str(), NULL, NULL)) < 0) {
		fprintf(stderr, "[failed] Could not open input file\n");
		ret = ErrorNo_FileOpenFail;
		goto end;
	}

	if ((ret = avformat_find_stream_info(p->ifmt_ctx, 0)) < 0) {
		fprintf(stderr, "[failed] Failed to retrieve input stream information\n");
		ret = ErrorNo_Unknow;
		goto end;
	}

	av_dump_format(p->ifmt_ctx, 0, p->filename.c_str(), 0);

	return 0;
end:
	if (p->ifmt_ctx){
		avformat_close_input(&p->ifmt_ctx);
		p->ifmt_ctx = NULL;
	}
	return ret;
}

int mp4Writer::bind_stream(InputCfg_S *in, OutputCfg_S *out, int type, int &inIndex,int &outIndex)
{
	int ret;
	for (int i = 0; i < in->ifmt_ctx->nb_streams; i++) {
		//Create output AVStream according to input AVStream
		if (in->ifmt_ctx->streams[i]->codec->codec_type == type){
			AVStream *in_stream = in->ifmt_ctx->streams[i];
			AVStream *out_stream = avformat_new_stream(out->ofmt_ctx, in_stream->codec->codec);
			inIndex = i;
			if (!out_stream) {
				fprintf(stderr, "[failed] Failed allocating output stream\n");

				ret = ErrorNo_Unknow;
				goto end;
			}
			outIndex = out_stream->index;
			//Copy the settings of AVCodecContext
			if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
				printf("Failed to copy context from input to output stream codec context\n");
				ret = ErrorNo_Unknow;
				goto end;
			}
			out_stream->codec->codec_tag = 0;
			if (out->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
			break;
		}
	}

	return 0;
end:
	return ret;
}

int mp4Writer::open_output_file(OutputCfg_S *p, InputCfg_S *inV)
{
	int ret;

	avformat_alloc_output_context2(&p->ofmt_ctx, NULL, NULL, p->filename.c_str());
	if (!p->ofmt_ctx) {
		fprintf(stderr, "[failed] Could not create output context\n");

		ret = ErrorNo_Unknow;
		goto end;
	}
	p->ofmt = p->ofmt_ctx->oformat;

	ret = bind_stream(inV, p, AVMEDIA_TYPE_VIDEO, inV->indexVideo, p->indexVideo);
	ret |= bind_stream(inV, p, AVMEDIA_TYPE_AUDIO, inV->indexAudio,p->indexAudio);
	if (ret != 0)
	{
		goto end;
	}

	if (inV->indexVideo == -1 || inV->indexAudio == -1 || p->indexAudio == -1 || p->indexVideo == -1){
		ret = ErrorNo_NoVideoOrAudio;
		if (inV->indexVideo == -1)
			fprintf(stderr, "[failed] no video stream\n");
		if (inV->indexAudio == -1)
			fprintf(stderr, "[failed] no audio stream\n");
		goto end;
	}

	//Open output file
	if (!(p->ofmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&p->ofmt_ctx->pb, p->filename.c_str(), AVIO_FLAG_WRITE) < 0) {
			printf("Could not open output file '%s'", p->filename.c_str());
			ret = ErrorNo_FileOpenFail;
			goto end;
		}
	}

	return 0;
end:
	if (p->ofmt_ctx)
	{
		if (p->ofmt_ctx && !(p->ofmt->flags & AVFMT_NOFILE))
			avio_close(p->ofmt_ctx->pb);
		avformat_free_context(p->ofmt_ctx);
		p->ofmt_ctx = NULL;
	}
	return ret;
}

int mp4Writer::init()
{
	int ret;
	//FIX
#if USE_H264BSF
	AVBitStreamFilterContext* h264bsfc = av_bitstream_filter_init("h264_mp4toannexb");
#endif

#if USE_AACBSF
	AVBitStreamFilterContext* aacbsfc = av_bitstream_filter_init("aac_adtstoasc");
#endif

	av_register_all();

	ret = open_input_file(&_inputCfg, fp_open_v, "");
	if (ret != 0){
		goto end;
	}

	ret = open_output_file(&_outputCfg, &_inputCfg);
	if (ret != 0){
		goto end;
	}
		
	return 0;
end:
	return ret;
}

int mp4Writer::cleanup()
{
	if (_inputCfg.ifmt_ctx){
		avformat_close_input(&_inputCfg.ifmt_ctx);
	}
	/* close output */
	if (_outputCfg.ofmt_ctx && !(_outputCfg.ofmt->flags & AVFMT_NOFILE))
		avio_close(_outputCfg.ofmt_ctx->pb);
	if (_outputCfg.ofmt_ctx){
		avformat_free_context(_outputCfg.ofmt_ctx);
	}

	return 0;
}
extern "C" AVRational av_stream_get_codec_timebase(const AVStream *st);
int mp4Writer::writeMp4(Config &cfg)
{
	CTSAdjust *p = new CTSAdjust();
	int ret = 0;
	_inputCfg.filename = cfg._inputFile;
	_outputCfg.filename = cfg._outputFile;

	ret = init();
	if (ret != 0){
		cleanup();
		return ret;
	}

	int64_t cur_pts_v = -1;
	int64_t cur_pts_a = -1;
	int frame_index = 0;

	// timebase check
	AVRational tTimebase;
	for (int i = 0; i < 2; i++){
		tTimebase = av_stream_get_codec_timebase(_outputCfg.ofmt_ctx->streams[i]);
		printf("stream[%d] before timebase.num=%d timebase.den=%d\n", i, tTimebase.num, tTimebase.den);
		if (tTimebase.den <= 0 || 
			tTimebase.den > 1000000 || 
			tTimebase.num <= 0 ||
			tTimebase.num > 1000000)
		{
			_outputCfg.ofmt_ctx->streams[i]->time_base.num = 1;
			_outputCfg.ofmt_ctx->streams[i]->time_base.den = 10000;
		}
	}

	//Write file header
	if (avformat_write_header(_outputCfg.ofmt_ctx, NULL) < 0) {
		fprintf(stderr, "[failed] Error occurred when opening output file\n");
		ret = ErrorNo_Unknow;
		goto end;
	}

	for (int i = 0; i < 2; i++){
		tTimebase = _outputCfg.ofmt_ctx->streams[i]->time_base;
		printf("stream[%d] after timebase.num=%d timebase.den=%d\n", i, tTimebase.num, tTimebase.den);
	}

	AVPacket pkt;
	AVFormatContext *ifmt_ctx;

	ifmt_ctx = _inputCfg.ifmt_ctx;
	while (1){
		
		int stream_index = 0;
		AVStream *in_stream, *out_stream;

		av_init_packet(&pkt);
		pkt.data = NULL;
		pkt.size = 0;
		if (av_read_frame(ifmt_ctx, &pkt) < 0)
			break;

		printf("Write 1 Packet. size:%5d\tpts:%lld\tdts:%lld\t%d\n", pkt.size, pkt.pts, pkt.dts, pkt.stream_index);
		if (pkt.stream_index == _inputCfg.indexVideo){
			stream_index = _outputCfg.indexVideo;
			in_stream = ifmt_ctx->streams[pkt.stream_index];
			out_stream = _outputCfg.ofmt_ctx->streams[stream_index];
			//cout << "video : " << pkt.pts << " "<<pkt.dts<<" ";
			int64_t tmp = pkt.pts;
			pkt.pts =  p->AdjustV(pkt.pts);
			pkt.dts = pkt.dts - (tmp - pkt.pts);
			//cout << pkt.pts <<" " <<pkt.dts<<endl;
			nal_parser(&pkt);
		}else{
			stream_index = _outputCfg.indexAudio;
			in_stream = ifmt_ctx->streams[pkt.stream_index];
			out_stream = _outputCfg.ofmt_ctx->streams[stream_index];
			//cout << "audio : " << pkt.pts << " " << pkt.dts << " ";
			int64_t tmp = pkt.pts;
			pkt.pts = p->AdjustA(pkt.pts);
			pkt.dts = pkt.dts - (tmp - pkt.pts);
			//cout << pkt.pts << " " << pkt.dts << endl;
		}

		in_stream = ifmt_ctx->streams[pkt.stream_index];

		//Convert PTS/DTS
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		pkt.stream_index = stream_index;
#if 0
		printf("Write 1 Packet. size:%5d\tpts:%lld\tdts:%lld\n", pkt.size, pkt.pts, pkt.dts);
		//Write
		if (av_interleaved_write_frame(_outputCfg.ofmt_ctx, &pkt) < 0) {
			fprintf(stderr, "Error muxing packet\n");
			//break;
		}
#endif
		av_free_packet(&pkt);
	}

	//Write file trailer
	av_write_trailer(_outputCfg.ofmt_ctx);

#if USE_H264BSF
	av_bitstream_filter_close(h264bsfc);
#endif
#if USE_AACBSF
	av_bitstream_filter_close(aacbsfc);
#endif

	return 0;
end:
	cleanup();
	return ret;
}

bool mp4Writer::is_delete(unsigned char type)
{
	static int ppsFlag = 0;
	static int spsFlag = 0;
	static int seiFlag = 0;

	switch (type)
	{
	case 6:
	{
		if (seiFlag == 0){
			seiFlag = 1;
			return false;
		}
		else{
			return true;
		}
	}
		break;
	case 7:
		if (spsFlag == 0){
			spsFlag = 1;
			return false;
		}
		else{
			return true;
		}
		break;
	case 8:
		if (ppsFlag == 0){
			ppsFlag = 1;
			return false;
		}
		else{
			return true;
		}
		break;
	default:
		return false;
		break;
	}

	return 0;
}

int mp4Writer::get_data_size(unsigned char *pHead)
{
	char cSize[4] = { 0 };
	int *pDataSize = (int *)cSize;
	cSize[3] = pHead[0];
	cSize[2] = pHead[1];
	cSize[1] = pHead[2];
	cSize[0] = pHead[3];
	int size = *pDataSize;
	return size;
}

int mp4Writer::set_data_size(unsigned char *pHead, int size)
{
	char cSize[4] = { 0 };
	int *pDataSize = (int *)cSize;
	*pDataSize = size;
	pHead[0] = cSize[3];
	pHead[1] = cSize[2];
	pHead[2] = cSize[1];
	pHead[3] = cSize[0];

	return 0;
}

int mp4Writer::nal_parser_sub(unsigned char *pNal, nal_s &node)
{
	std::list<nal_s> lSons;
	std::vector<nal_s> &vSons = node.vecSons;
	int size = node.size + 4;
	// �ҵ����е�0x00000001
	for (int i = 0; i < size; i++){
		if (pNal[i] == 0x00 && pNal[i + 1] == 0x00 && pNal[i + 2] == 0x00 && pNal[i + 3] == 0x01){
			nal_s ns = { 0 };
			ns.posHead = i;
			ns.type = pNal[i + 4] & 0x1f;
			ns.bHasStartCode = true;
			lSons.push_back(ns);
		}
	}

	// û���ҵ� 0x00000001���������Ϊֻ����һ��NalU
	if (lSons.size() == 0){
		nal_s ns;
		ns.posHead = node.posHead;
		ns.type = pNal[4] & 0x1f;
		ns.bHasStartCode = false;
		ns.size = size - 4;
		vSons.push_back(ns);
		return 0;
	}

	// �����һ��NalU
	if (lSons.front().posHead == 4){ // ��һ��NalU����0x00000001,����λ��
		lSons.front().posHead = 0;
	}else{ // ������0x00000001�����׸�NalUû��0x00000001��Ҳ���Ƕ�slice�����
		nal_s ns;
		ns.posHead = 0;
		ns.type = pNal[4] & 0x1f;
		ns.bHasStartCode = false;
		lSons.push_front(ns);
	}

	// ���Ƶ�node��
	std::list<nal_s>::iterator iter = lSons.begin();
	for (; iter != lSons.end(); iter++){
		vSons.push_back(*iter);
	}
#if 0
	// ֻ��һ��nalu
	if (vSons.size() == 1){
		vSons[0].size = size - 4;
		vSons[0].posHead += node.posHead;
		return 0;
	}
#endif
	// ����size��Ϣ
	int i;
	for (i = 0; i < vSons.size() - 1; i++){
		vSons[i].size = vSons[i + 1].posHead - vSons[i].posHead - 4;
	}
	vSons[i].size = size - vSons[i].posHead - 4;

	// ���ɾ���λ��
	for (i = 0; i < vSons.size(); i++){
		vSons[i].posHead += node.posHead;
	}

	// ͳһȥ��startcode�����˵�һ��
	for (i = 1; i < vSons.size(); i++){
		vSons[i].bHasStartCode = false;
	}

	return 0;
}

int mp4Writer::nal_parser(AVPacket *org)
{
	//return 0;
	if (org->data == NULL || org->size <= 0)
		return 0;
	static int iii = 0;

	std::vector<nal_s> vecNalInfo;
	unsigned char *pNal = org->data;
	for (int i = 0; i < org->size;){
		nal_s ns = { 0 };
		ns.posHead = i;
		ns.type = -1;
		ns.size = get_data_size(&pNal[ns.posHead]);

		if (ns.size > org->size || ns.size<=0)
			return 0;
		nal_parser_sub(&pNal[ns.posHead], ns);
		vecNalInfo.push_back(ns);

		i += 4;
		i += ns.size;
	}
#if 0
	{
		printf("org->size:%d\n", org->size);
		for (int i = 0; i < vecNalInfo.size(); i++){
			printf("Nal%d\n", i);
			// ����
			std::vector<nal_s> &vSons = vecNalInfo[i].vecSons;
			for (int sIdx = 0; sIdx < vSons.size(); sIdx++){
				nal_s &ns = vSons[sIdx];
				printf("%d %d %d\n", ns.type, ns.posData, ns.size);
			}
		}

	}
#endif
	// ��������
	memcpy(_buffer, org->data, org->size);
	int curPos = 0;
	int totalSize = 0;
	for (int i = 0; i < vecNalInfo.size(); i++){

		// ����
		std::vector<nal_s> &vSons = vecNalInfo[i].vecSons;
		int naluTotalSize = vecNalInfo[i].size;
		int curPktuFirstPos = curPos;
		bool isWriteFirst = false;
		int delCount = 0;
		for (int sIdx = 0; sIdx < vSons.size(); sIdx++){
			nal_s &ns = vSons[sIdx];
			if (is_delete(ns.type) == true){

			}else{
				if (ns.bHasStartCode == false){
					memcpy(&org->data[curPos + 4], &_buffer[ns.posHead+4], ns.size);
					set_data_size(&org->data[curPos], ns.size);
					curPos += ns.size;
					curPos += 4;
					totalSize = totalSize + ns.size + 4;
				}
				else{ //bHasStartCode==true
					memcpy(&org->data[curPos + 4], &_buffer[ns.posHead + 8], ns.size - 4);
					set_data_size(&org->data[curPos], ns.size-4);
					curPos += ns.size;
					totalSize = totalSize + ns.size;
				}
			}
		}
	}
	org->size = totalSize;
	//printf("cur size:%d\n\n", org->size);

	return 0;
}
