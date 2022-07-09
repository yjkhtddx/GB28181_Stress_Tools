#pragma once
#include <iostream>
#include "Device.h"
#include "pugixml.hpp"
#include <vector>
#include <time.h>
#include <sstream>
#include "gb28181_header_maker.h"
using namespace std;

static int start_port = 40000;
static int SN_MAX = 99999999;
static int sn;
static int get_port() {
	start_port++;
	return start_port;
}
static int get_sn() {
	if (sn >= SN_MAX) {
		sn = 0;
	}
	sn++;
	return sn;
}

void Device::mobile_position_task() {
	while (is_running && is_mobile_position_running) {
		{
			osip_message_t * notify_message = NULL;
			ExosipCtxLock lock(sip_context);
			if (OSIP_SUCCESS != eXosip_insubscription_build_notify(sip_context, mobile_postition_dialog_id, EXOSIP_SUBCRSTATE_PENDING, EXOSIP_NOTIFY_PENDING, &notify_message)) {
				std::cout << "eXosip_insubscription_build_notify error" << std::endl;
				break;
			}
			stringstream ss;
			ss << "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
			ss << "<Notify>\r\n";
			ss << "<DeviceID>" << deviceId << "</DeviceID>\r\n";
			ss << "<CmdType>MobilePosition</CmdType>\r\n";
			ss << "<<SN>" + mobile_position_sn << "</SN>\r\n";
			ss << "<Time>" << "</Time>\r\n";
			ss << "<Longitude>" << "116.405994" << "</Longitude>\r\n";
			ss << "<Latitude>" << "39.914492" << "</Latitude>\r\n";
			ss << "<Speed>0.0</Speed>\r\n";
			ss << "<Direction>0.0</Direction>\r\n";
			ss << "<Altitude>0.0</Altitude>\r\n";
			ss << "</Notify>\r\n";
			osip_message_set_content_type(notify_message, "Application/MANSCDP+xml");
			osip_message_set_body(notify_message, ss.str().c_str(), strlen(ss.str().c_str()));
			eXosip_insubscription_send_request(sip_context, mobile_postition_dialog_id, notify_message);
		}
		//eXosip_subscription_send_refresh_request(sip_context, mobile_postition_dialog_id, notify_message);
		//this_thread::sleep_for(std::chrono::seconds(5));
		std::unique_lock<std::mutex> lck(_mobile_position_mutex);
		_mobile_postion_condition.wait_for(lck,std::chrono::seconds(5));
	}
}

void Device::create_heartbeat_task()
{
	if (heartbeat_thread) {
		is_heartbeat_running = false;
		if (heartbeat_thread && heartbeat_thread->joinable()) {
			_heartbeat_condition.notify_one();
			heartbeat_thread->join();
		}
	}
	is_heartbeat_running = true;
	heartbeat_thread = std::make_shared<std::thread>(&Device::heartbeat_task, this);
}

void Device::create_push_stream_task()
{
	if (push_stream_thread) {
		is_pushing = false;
		if (push_stream_thread->joinable()){
			push_stream_thread->join();
		}
	}
	is_pushing = true;
	push_stream_thread = std::make_shared<std::thread>(&Device::push_task, this);
}

void Device::create_mobile_position_task() {
	if (mobile_position_thread) {
		is_mobile_position_running = false;
		if (mobile_position_thread->joinable()) {
			_mobile_postion_condition.notify_one();
			mobile_position_thread->join();
		}
	}
	is_mobile_position_running = true;
	mobile_position_thread = std::make_shared<std::thread>(&Device::mobile_position_task, this);
}

void Device::process_call(eXosip_event_t * evt)
{
	//����sdp
	osip_body_t *sdp_body = NULL;
	osip_message_get_body(evt->request, 0, &sdp_body);
	if (sdp_body != NULL) {
		printf("request >>> %s", sdp_body->body);
	}
	else {
		cout << "sdp error" << endl;
		return;
	}
	sdp_message_t * sdp = NULL;

	if (OSIP_SUCCESS != sdp_message_init(&sdp)) {
		cout << "sdp_message_init failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"sdp_message_init failed" });
		}
		return;
	}
	if (OSIP_SUCCESS != sdp_message_parse(sdp, sdp_body->body)) {
		cout << "sdp_message_parse failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"sdp_message_parse failed" });
		}
		return;
	}
	sdp_connection_t * connect = eXosip_get_video_connection(sdp);
	sdp_media_t * media = eXosip_get_video_media(sdp);
	target_ip = connect->c_addr;
	target_port = atoi(media->m_port);
	char * protocol = media->m_proto;
	is_tcp = strstr(protocol, "TCP");
	if (callback != nullptr) {
		char port[5];
		snprintf(port, 5, "%d", target_port);
		callback(list_index, Message{ PULL_STREAM_PROTOCOL_TYPE ,is_tcp ? "TCP" : "UDP" });
		callback(list_index, Message{ PULL_STREAM_PORT_TYPE ,port });
	}
	//����200_ok
	listen_port = get_port();
	char port[10];
	snprintf(port, 10, "%d", listen_port);
	if (callback != nullptr) {
		callback(list_index, Message{ PULL_STREAM_PORT_TYPE ,port });
	}
	stringstream ss;
	ss << "v=0\r\n";
	ss << "o=" << videoChannelId << " 0 0 IN IP4 " << local_ip << "\r\n";
	ss << "s=Play\r\n";
	ss << "c=IN IP4 " << local_ip << "\r\n";
	ss << "t=0 0\r\n";
	if (is_tcp) {
		ss << "m=video " << listen_port << " TCP/RTP/AVP 96\r\n";
	}
	else {
		ss << "m=video " << listen_port << " RTP/AVP 96\r\n";
	}
	ss << "a=sendonly\r\n";
	ss << "a=rtpmap:96 PS/90000\r\n";
	ss << "y=4294967295\r\n";
	string sdp_str = ss.str();


	size_t size = sdp_str.size();

	osip_message_t * message = evt->request;
	int status = eXosip_call_build_answer(sip_context, evt->tid, 200, &message);

	if (status != 0) {
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"�ظ�inviteʧ��" });
		}
		return;
	}

	osip_message_set_content_type(message, "APPLICATION/SDP");
	osip_message_set_body(message, sdp_str.c_str(), sdp_str.size());

	eXosip_call_send_answer(sip_context, evt->tid, 200, message);

	cout << "reply sdp " << sdp_str.c_str() << endl;
}

void Device::heartbeat_task() {
	while (is_running && register_success && is_heartbeat_running) {
		stringstream ss;
		ss << "<?xml version=\"1.0\"?>\r\n";
		ss << "<Notify>\r\n";
		ss << "<CmdType>Keepalive</CmdType>\r\n";
		ss << "<SN>" << get_sn() << "</SN>\r\n";
		ss << "<DeviceID>" << deviceId << "</DeviceID>\r\n";
		ss << "<Status>OK</Status>\r\n";
		ss << "</Notify>\r\n";

		osip_message_t* request = create_request();
		if (request != NULL) {
			osip_message_set_content_type(request, "Application/MANSCDP+xml");
			osip_message_set_body(request, ss.str().c_str(), strlen(ss.str().c_str()));
			int tid = send_request(request);
			if (tid > 0) {
				std::lock_guard<std::mutex> guard(_heartbeat_map_mutex);
				_heartbeat_map[tid] = clock();
			}
		}
		//std::this_thread::sleep_for(std::chrono::seconds(60));
		std::unique_lock<std::mutex> lck(_heartbeat_mutex);
		_heartbeat_condition.wait_for(lck, std::chrono::milliseconds(this->keepalive_cycle_ms));
	}

}
void Device::push_task() {

	udp_client = new UDPClient(is_tcp);

	int status = is_tcp ? udp_client->bind(local_ip, listen_port, target_ip, target_port) : udp_client->bind(local_ip, listen_port);

	if (0 != status) {
		cout << "client bind port failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"�󶨱��ض˿ڻ�������ʧ��" });
		}
		if (callId != -1 && dialogId != -1) {
			ExosipCtxLock lock(sip_context);
			eXosip_call_terminate(sip_context, callId, dialogId);
		}
		return;
	}

	char ps_header[PS_HDR_LEN];

	char ps_system_header[SYS_HDR_LEN];

	char ps_map_header[PSM_HDR_LEN];

	char pes_header[PES_HDR_LEN];

	char rtp_header[RTP_HDR_LEN];

	int time_base = 90000;
	int fps = 25;
	int send_packet_interval = 1000 / fps;

	int interval = time_base / fps;
	long pts = 0;

	char frame[1024 * 128];

	int single_packet_max_length = 1400;

	char rtp_packet[RTP_HDR_LEN + 1400];

	int ssrc = 0xffffffff;
	int rtp_seq = 0;

	extern std::vector<Nalu*> nalu_vector;
	size_t size = nalu_vector.size();

	while (is_pushing)
	{
		for (int i = 0; i < size; i++) {
			if (!is_pushing) {
				break;
			}
			Nalu *nalu = nalu_vector.at(i);

			NaluType  type = nalu->type;
			int length = nalu->length;
			char * packet = nalu->packet;

			int index = 0;
			if (NALU_TYPE_IDR == type) {

				gb28181_make_ps_header(ps_header, pts);

				memcpy(frame, ps_header, PS_HDR_LEN);
				index += PS_HDR_LEN;

				gb28181_make_sys_header(ps_system_header, 0x3f);

				memcpy(frame + index, ps_system_header, SYS_HDR_LEN);
				index += SYS_HDR_LEN;

				gb28181_make_psm_header(ps_map_header);

				memcpy(frame + index, ps_map_header, PSM_HDR_LEN);
				index += PSM_HDR_LEN;

			}
			else {
				gb28181_make_ps_header(ps_header, pts);

				memcpy(frame, ps_header, PS_HDR_LEN);
				index += PS_HDR_LEN;
			}

			//��װpes
			gb28181_make_pes_header(pes_header, 0xe0, length, pts, pts);

			memcpy(frame + index, pes_header, PES_HDR_LEN);
			index += PES_HDR_LEN;


			memcpy(frame + index, packet, length);
			index += length;

			//���rtp

			int rtp_packet_count = ((index - 1) / single_packet_max_length) + 1;

			for (int i = 0; i < rtp_packet_count; i++) {

				gb28181_make_rtp_header(rtp_header, rtp_seq, pts, ssrc, i == (rtp_packet_count - 1));

				int writed_count = single_packet_max_length;

				if ((i + 1)*single_packet_max_length > index) {
					writed_count = index - (i* single_packet_max_length);
				}
				//��Ӱ����ֽ�
				int rtp_start_index = 0;

				unsigned short rtp_packet_length = RTP_HDR_LEN + writed_count;
				if (is_tcp) {
					unsigned char  packt_length_ary[2];
					packt_length_ary[0] = (rtp_packet_length >> 8) & 0xff;
					packt_length_ary[1] = rtp_packet_length & 0xff;
					memcpy(rtp_packet, packt_length_ary, 2);
					rtp_start_index = 2;
				}
				memcpy(rtp_packet + rtp_start_index, rtp_header, RTP_HDR_LEN);
				memcpy(rtp_packet + +rtp_start_index + RTP_HDR_LEN, frame + (i* single_packet_max_length), writed_count);
				rtp_seq++;

				udp_client->send_packet(target_ip, target_port, rtp_packet, rtp_start_index + rtp_packet_length);
			}

			pts += interval;
			/*ULONGLONG end_time = GetTickCount64();
			ULONGLONG dis = end_time - start_time;
			if (dis < send_packet_interval) {
				std::this_thread::sleep_for(std::chrono::milliseconds(send_packet_interval -dis));
			}*/
			std::this_thread::sleep_for(std::chrono::milliseconds(38));

		}
	}
	if (udp_client != nullptr) {
		udp_client->release();
		delete udp_client;
		udp_client = nullptr;
	}
}

int Device::send_request(osip_message_t * request) {
	int tid;
	eXosip_lock(sip_context);
	tid = eXosip_message_send_request(sip_context, request);
	eXosip_unlock(sip_context);
	return tid;
}

osip_message_t* Device::create_request() {

	osip_message_t* request = NULL;

	char fromSip[256] = { 0 };
	char toSip[256] = { 0 };

	if (!is_running) {
		return nullptr;
	}

	sprintf(fromSip, "<sip:%s@%s:%d>", deviceId, local_ip, local_port);

	sprintf(toSip, "<sip:%s@%s:%d>", server_sip_id, server_ip, server_port);

	int status = eXosip_message_build_request(sip_context,
		&request, "MESSAGE", toSip, fromSip, NULL);
	if (OSIP_SUCCESS != status) {
		std::cout << "build requests failed" << std::endl;
	}

	return request;

}

void Device::send_response(eXosip_event_t *evt, osip_message_t * message) {
	eXosip_lock(sip_context);
	eXosip_message_send_answer(sip_context, evt->tid, 200, message);
	eXosip_unlock(sip_context);
}

void Device::send_response_ok(eXosip_event_t *evt) {
	osip_message_t * message = evt->request;
	eXosip_message_build_answer(sip_context, evt->tid, 200, &message);
	send_response(evt, message);
}
void print_request(osip_message_t * request_message) {
	char *dest = NULL;
	size_t length = 0;
	int i = osip_message_to_str(request_message, &dest, &length);
	if (i == 0)
	{
		std::cout << dest << std::endl;
		delete[] dest;
		//osip_free(dest);
	}
	else {
		std::cout << "osip_message_to_str failed" << endl;
	}
}
void Device::process_request() {
	eXosip_event_t *evt = NULL;
	while (is_running)
	{
		evt = eXosip_event_wait(sip_context, 0, 50);
		eXosip_lock(sip_context);
		eXosip_automatic_action(sip_context);
		eXosip_unlock(sip_context);
		if (evt == NULL) {
			continue;
		}
		//print_request(evt->request);
		switch (evt->type) {

		case EXOSIP_IN_SUBSCRIPTION_NEW: {
			//struct eXosip_t *excontext, int tid, int status, osip_message_t * answer
			ExosipCtxLock lolck(sip_context);
			osip_message_t * answer = NULL;
			if (OSIP_SUCCESS != eXosip_insubscription_build_answer(sip_context, evt->tid, 200, &answer)) {
				eXosip_unlock(sip_context);
				break;
			}
			eXosip_insubscription_send_answer(sip_context, evt->tid, 200, answer);
			mobile_postition_dialog_id = evt->did;
			create_mobile_position_task();
			break;
		}

		case EXOSIP_MESSAGE_NEW: {
			if (MSG_IS_MESSAGE(evt->request)) {
				osip_body_t *body = NULL;
				osip_message_get_body(evt->request, 0, &body);
				if (body != NULL) {
					printf("request >>> %s", body->body);
				}

				send_response_ok(evt);

				pugi::xml_document doucument;
				if (!doucument.load(body->body)) {
					cout << "load xml failed" << endl;
					break;
				}
				pugi::xml_node root_node = doucument.first_child();

				if (!root_node) {
					cout << "root node get failed" << endl;
					break;
				}
				string root_name = root_node.name();
				if ("Query" == root_name) {
					pugi::xml_node cmd_node = root_node.child("CmdType");

					if (!cmd_node) {
						cout << "root node get failed" << endl;
						break;
					}

					pugi::xml_node sn_node = root_node.child("SN");
					string  cmd = cmd_node.child_value();
					if ("Catalog" == cmd) {
						stringstream ss;
						ss << "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
						ss << "<Response>\r\n";
						ss << "<CmdType>Catalog</CmdType>\r\n";
						ss << "<SN>" << sn_node.child_value() << "</SN>\r\n";
						ss << "<DeviceID>" << deviceId << "</DeviceID>\r\n";
						ss << "<SumNum>" << 1 << "</SumNum>\r\n";
						ss << "<DeviceList Num=\"" << 1 << "\">\r\n";
						ss << "<Item>\r\n";
						ss << "<DeviceID>" << videoChannelId << "</DeviceID>\r\n";
						ss << "<Name>IPC</Name>\r\n";
						ss << "<ParentID>" << server_sip_id << "</ParentID>\r\n";
						ss << "</Item>\r\n";
						ss << "</DeviceList>\r\n";
						ss << "</Response>\r\n";
						osip_message_t* request = create_request();
						if (request != NULL) {
							osip_message_set_content_type(request, "Application/MANSCDP+xml");
							osip_message_set_body(request, ss.str().c_str(), strlen(ss.str().c_str()));
							send_request(request);
						}
					}
					else if ("RecordInfo" == cmd) {
						//processRecordInfo(root_note);
					}

				}
			}
			else if (MSG_IS_BYE(evt->request)) {

				std::cout << "accept bye" << std::endl;

			}
			break;
		}
		case EXOSIP_REGISTRATION_SUCCESS:
		{
			cout << "ע��ɹ�" << endl;
			if (callback != nullptr) {
				callback(list_index, Message{ STATUS_TYPE ,"ע��ɹ�" });
			}
			register_success = true;
			if (heartbeat_thread) {
				break;
			}
			create_heartbeat_task();
			break;
		}
		case EXOSIP_REGISTRATION_FAILURE: {
			register_success = false;
			if (evt->response == NULL) {
				return;
			}
			if (401 == evt->response->status_code) {
				if (callback != nullptr) {
					callback(list_index, Message{ STATUS_TYPE ,"ע�� 401" });
				}
				osip_www_authenticate_t* www_authenticate_header;

				osip_message_get_www_authenticate(evt->response, 0, &www_authenticate_header);

				//struct eXosip_t *excontext, const char *username, const char *userid, const char *passwd, const char *ha1, const char *realm
				eXosip_add_authentication_info(sip_context, deviceId, deviceId, password, "MD5", www_authenticate_header->realm);
			}
			break;
		}
		case EXOSIP_CALL_ACK: {
			//������
			cout << "���յ� ack����ʼ����" << endl;
			callId = evt->cid;
			dialogId = evt->did;

			if (callback != nullptr) {
				callback(list_index, Message{ STATUS_TYPE ,"��ʼ����" });
			}
			create_push_stream_task();
			break;
		}
		case EXOSIP_CALL_CLOSED: {
			cout << "���յ�Bye����������" << endl;
			callId = -1;
			dialogId = -1;
			is_pushing = false;
			if (callback != nullptr) {
				callback(list_index, Message{ STATUS_TYPE ,"��������" });
			}
			break;
		}
		case EXOSIP_CALL_INVITE: {
			process_call(evt);
			break;
			}
		case EXOSIP_MESSAGE_ANSWERED: {
			printf("��Ϣ��Ӧ=>%s()\n[%s:%d]\n", __FUNCTION__, __FILE__, __LINE__);
			int tid = evt->tid;
			clock_t diff = -1;
			if (tid > 0 ) {
				std::lock_guard<std::mutex> guard(_heartbeat_map_mutex);
				if (_heartbeat_map.count(tid) == 1) {
					diff =  clock() - _heartbeat_map[tid] ;
					printf("��Ӧʱ��Ϊ[ %d ms ]=>%s()\n[%s:%d]\n",diff, __FUNCTION__, __FILE__, __LINE__);
				}
			}
			
			if (diff > 0) {
				callback(list_index, Message{ RES_TIME ,to_string(diff).c_str()});
			}

			break;
			}
		}


		if (evt != NULL) {
			eXosip_event_free(evt);
			evt = NULL;
		}
	}
	if (is_running) {
		cout << "sip �쳣�˳�" << endl;
	}
}



void Device::start_sip_client(int local_port) {
	this->local_port = local_port;
	sip_context = eXosip_malloc();

	if (OSIP_SUCCESS != eXosip_init(sip_context)) {
		cout << "sip init failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"exo_sip init failed" });
		}
		return;
	}

	if (OSIP_SUCCESS != eXosip_listen_addr(sip_context, IPPROTO_UDP, NULL, local_port, AF_INET, 0)) {
		cout << "sip bind port failed" << endl;
		if (callback != nullptr) {
			callback(list_index, Message{ STATUS_TYPE ,"sip bind port failed" });
		}
		eXosip_quit(sip_context);
		sip_context = nullptr;
		return;
	}
	is_running = true;

	sip_thread = std::make_shared<std::thread>(&Device::process_request, this);

	char from_uri[128] = { 0 };
	char proxy_uri[128] = { 0 };
	char contact[128] = { 0 };

	eXosip_guess_localip(sip_context, AF_INET, local_ip, 128);
	//������<>��������ȻeXosip_register_build_initial_register ��һֱ���� -5���﷨����
	sprintf(from_uri, "sip:%s@%s:%d", deviceId, local_ip, local_port);
	sprintf(contact, "sip:%s@%s:%d", deviceId, local_ip, local_port);
	//sprintf(contact,"<sip:%s@%s:%d>",deviceId,server_ip,server_port);
	sprintf(proxy_uri, "sip:%s@%s:%d", server_sip_id, server_ip, server_port);

	eXosip_clear_authentication_info(sip_context);


	osip_message_t * register_message = NULL;
	//struct eXosip_t *excontext, const char *from, const char *proxy, const char *contact, int expires, osip_message_t ** reg
	int register_id = eXosip_register_build_initial_register(sip_context, from_uri, proxy_uri, contact, 3600, &register_message);
	if (register_message == NULL) {
		cout << "eXosip_register_build_initial_register failed" << endl;
		return;
	}
	//��ǰ��������֤��Ϣ������ϢΪ401������eXosip_automatic_action()�Զ�����
	//eXosip_add_authentication_info(sip_context,"022000000110000", "022000000110000", "12345678", "MD5", NULL);
	eXosip_lock(sip_context);
	eXosip_register_send_register(sip_context, register_id, register_message);
	eXosip_unlock(sip_context);
	if (callback != nullptr) {
		callback(list_index, Message{ STATUS_TYPE ,"����ע����Ϣ" });
	}
}

void Device::set_callback(std::function<void(int index, Message msg)> callback) {
	this->callback = std::move(callback);
}

Device::~Device()
{
	is_running = false;
	if (sip_thread) {
		sip_thread->join();
		if (sip_context) {
			eXosip_quit(sip_context);
			sip_context = NULL;
		}
	}
	register_success = false;
	is_heartbeat_running = false;
	if (heartbeat_thread && heartbeat_thread->joinable()) {
		_heartbeat_condition.notify_one();
		heartbeat_thread->join();
	}
	is_mobile_position_running = false;
	if (mobile_position_thread && mobile_position_thread->joinable()) {
		_mobile_postion_condition.notify_one();
		mobile_position_thread->join();
	}

	is_pushing = false;
	if (push_stream_thread && push_stream_thread->joinable()){
		push_stream_thread->join();
	}
	if (callback != nullptr) {
		callback(list_index, Message{ STATUS_TYPE ,"�ͷ��豸" });
	}
}
