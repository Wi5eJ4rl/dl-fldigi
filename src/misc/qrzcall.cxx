// ----------------------------------------------------------------------------
// qrzcall.cxx  -- a part of fldigi
//
// Copyright (C) 2006
//		Dave Freese, W1HKJ
//		Leigh Klotz, WA5ZNU
//
// This file is part of fldigi.
//
// fldigi is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// fldigi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// ----------------------------------------------------------------------------

#include <config.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <math.h>
#include <ctype.h>

#include <FL/fl_ask.H>

#include "threads.h"

#include "misc.h"
#include "configuration.h"

#include "qrzcall.h"
#include "main.h"
#include "confdialog.h"
#include "fl_digi.h"
#include "qrzlib.h"

#include "xmlreader.h"

using namespace std;

int rotoroffset = 0;

string htmlpage = "";
string xmlpage = "";
string sessionpage = "";
string host = "online.qrz.com";
string detail;
string qrzSessionKey;
string qrzalert;
string qrzerror;
string callsign = "";
string qrzname;
string qrzaddr1;
string qrzaddr2;
string qrzstate;
string qrzzip;
string qrzcountry;
string qrzborn;
string qrzfname;
string qrzqth;
string qrzgrid;
string qrzlatd;
string qrzlond;
string qrznotes;

const char *error[] = {
	"OK",								// err 0
	"Host not found",					// err 1
	"Not an IP host!",					// err 2
	"No http service",					// err 3
	"Cannot open socket",				// err 4
	"Cannot Connect to server",		 	// err 5
	"Socket write error",				// err 6
	"Socket timeout",					// err 7
	"Socket select error"				// err 8
};

enum QUERYTYPE { NONE, QRZCD, QRZNET, HAMCALLNET };
QUERYTYPE DB_query = NONE;

enum TAG { \
	IGNORE,	KEY,	ALERT,	ERROR,	CALL, \
	FNAME,	NAME,	ADDR1,	ADDR2,	STATE, \
	ZIP,	COUNTRY,LATD,	LOND,	GRID, \
	DOB };

char rbuffer[32768];

fd_set readfds, testfds;
struct timeval timeout;
int sockfd = -1;
int result;
struct sockaddr_in address;
struct hostent *hostinfo;
struct servent *servinfo;

int qrzdummy;
Fl_Thread QRZ_thread;
bool QRZ_exit = false;
bool QRZ_enabled = false;

static void *CALLSIGNloop(void *args);

bool parseSessionKey();
bool parse_xml();
int  getSessionKey();
int  QRZGetXML();
int  bearing(const char *, const char *);
void qra(const char *, double &, double &);
void QRZ_disp_result();
void QRZ_COM_query();
void HAMCALL_COM_query();
void QRZ_CD_query();
void QRZinit(void);
void QRZclose(void);
void qthappend(string &qth, string &datum);
void QRZAlert();
int  QRZLogin();
void QRZquery();
void parse_html();
int  HAMCALLget();
void HAMCALLquery();


QRZ *qCall;

bool parseSessionKey()
{
	IrrXMLReader* xml = createIrrXMLReader(new IIrrXMLStringReader(sessionpage));
	TAG tag=IGNORE;
	while(xml && xml->read()) {
		switch(xml->getNodeType())
		{
		case EXN_TEXT:
		case EXN_CDATA:
			switch (tag) 
			{
			default:
				break;
			case KEY:
				qrzSessionKey = xml->getNodeData();
				break;
			case ALERT:
				qrzalert = xml->getNodeData();
				break;
			case ERROR:
				qrzerror = xml->getNodeData();
				break;
			}
			break;

		case EXN_ELEMENT_END:
			tag=IGNORE;
			break;

		case EXN_ELEMENT:
		{
			const char *nodeName = xml->getNodeName();
			if (!strcmp("Key", nodeName)) tag=KEY;
			else if (!strcmp("Alert", nodeName)) tag=ALERT;
			else if (!strcmp("Error", nodeName)) tag=ERROR;
			else tag=IGNORE;
			break;
		}

		case EXN_NONE:
		case EXN_COMMENT:
		case EXN_UNKNOWN:
			break;
		}
	}
	delete xml;
	return 0;
} 


bool parse_xml()
{
	IrrXMLReader* xml = createIrrXMLReader(new IIrrXMLStringReader(xmlpage));

// If we got any result back, clear the session key so that it will be 
// refreshed by this response, or if not present, will be removed and we'll
// know to log in next time.
	if (xml) {
		qrzSessionKey="";
		qrzalert="";
		qrzerror="";
		qrzname="";
		qrzaddr1="";
		qrzaddr2="";
		qrzstate="";
		qrzzip="";
		qrzcountry="";
		qrzborn="";
		qrzfname="";
		qrzqth="";
		qrzgrid="";
		qrzlatd="";
		qrzlond="";
		qrznotes="";
	}
// strings for storing the data we want to get out of the file
	string	call, 
			fname, 
			name, 
			addr1, 
			addr2, 
			state, 
			zip, 
			country, 
			latd, 
			lond, 
			grid, 
			dob;
			
	TAG tag = IGNORE;
	
// parse the file until end reached
	while(xml && xml->read()) {
		switch(xml->getNodeType()) {
			case EXN_TEXT:
			case EXN_CDATA:
				switch (tag) {
					default:
					case IGNORE:
						break;
					case CALL:
						call = xml->getNodeData();
						break;
					case FNAME:
						qrzfname =  xml->getNodeData();
						break;
					case NAME:
						qrzname =  xml->getNodeData();
						break;
					case ADDR1:
						qrzaddr1 =  xml->getNodeData();
						break;
					case ADDR2:
						qrzaddr2 =  xml->getNodeData();
						break;
					case STATE:
						qrzstate =  xml->getNodeData();
						break;
					case ZIP:
						qrzzip =  xml->getNodeData();
						break;
					case COUNTRY:
						qrzcountry =  xml->getNodeData();
						break;
					case LATD:
						qrzlatd =  xml->getNodeData();
						break;
					case LOND:
						qrzlond =  xml->getNodeData();
						break;
					case GRID:
						qrzgrid =  xml->getNodeData();
						break;
					case DOB:
						qrznotes = "DOB: ";
						qrznotes += xml->getNodeData();
						break;
					case ALERT:
						qrzalert = xml->getNodeData();
						break;
					case ERROR:
						qrzerror = xml->getNodeData();
						break;
					case KEY:
						qrzSessionKey = xml->getNodeData();
						break;
				}
				break;
				
			case EXN_ELEMENT_END:
				tag=IGNORE;
				break;

			case EXN_ELEMENT: 
				{
				const char *nodeName = xml->getNodeName();
				if (!strcmp("call", nodeName)) 			tag = CALL;
				else if (!strcmp("fname", nodeName)) 	tag = FNAME;
				else if (!strcmp("name", nodeName)) 	tag = NAME;
				else if (!strcmp("addr1", nodeName)) 	tag = ADDR1;
				else if (!strcmp("addr2", nodeName)) 	tag = ADDR2;
				else if (!strcmp("state", nodeName)) 	tag = STATE;
				else if (!strcmp("zip", nodeName)) 		tag = ZIP;
				else if (!strcmp("country", nodeName))	tag = COUNTRY;
				else if (!strcmp("latd", nodeName)) 	tag = LATD;
				else if (!strcmp("lond", nodeName)) 	tag = LOND;
				else if (!strcmp("grid", nodeName)) 	tag = GRID;
				else if (!strcmp("dob", nodeName)) 		tag = DOB;
				else if (!strcmp("Alert", nodeName)) 	tag = ALERT;
				else if (!strcmp("Error", nodeName)) 	tag = ERROR;
				else if (!strcmp("Key", nodeName)) 		tag = KEY;
				else tag = IGNORE;
				}
				break;

			case EXN_NONE:
			case EXN_COMMENT:
			case EXN_UNKNOWN:
				break;
		}
	}

// delete the xml parser after usage
	delete xml;
	return 0;
}

int getSessionKey()
{
	hostinfo = gethostbyname(host.c_str());
	if(!hostinfo)
		return 1;
	if(hostinfo->h_addrtype != AF_INET)
		return 2;
	servinfo = getservbyname("http", "tcp");
	if(!servinfo)
		return 3;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (sockfd == -1)
		return 4;
	address.sin_family = AF_INET;
	address.sin_port = servinfo->s_port;
	address.sin_addr = *(struct in_addr *)*hostinfo->h_addr_list;

	result = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
	if(result == -1) {
		close(sockfd);
		return 5;
	}

	{
		detail = "GET /bin/xml?username=";
		detail += progdefaults.QRZusername;
		detail += ";password=";
		detail += progdefaults.QRZuserpassword;
		detail += ";version=";
		detail += PACKAGE_NAME;
		detail += "/";
		detail += PACKAGE_VERSION;
		detail += " HTTP/1.0\n";
		detail += "Host: ";
		detail += host;
		detail += "\n";
		detail += "Connection: close\n";
		detail += "\n";
	}
  
	
	result = write(sockfd, detail.c_str() , detail.length());
	if (result != (int)detail.length()) {
		close(sockfd);
		return 6;
	}

	FD_ZERO(&readfds);
	FD_SET(sockfd, &readfds);
	
	while (1) {
		testfds = readfds;
		timeout.tv_sec = 5;		// timeout = 5 seconds
		timeout.tv_usec = 0;
		result = select(FD_SETSIZE, &testfds, (fd_set *)0, (fd_set *)0, &timeout);
		if (result == 0) {
			break;
		}
		if (result == -1) {
			close(sockfd);
			return 8;
		}
		if (FD_ISSET(sockfd, &testfds)) {
			memset(rbuffer, 0, 32768);
			result = read(sockfd, rbuffer, sizeof(rbuffer));
			if (result <= 0) break;
			sessionpage += rbuffer;
		} else {
			break;
		}
	}
    
	close(sockfd);
	return 0;
}

int QRZGetXML()
{
	hostinfo = gethostbyname(host.c_str());
	if(!hostinfo)
		return 1;
	if(hostinfo->h_addrtype != AF_INET)
		return 2;
	servinfo = getservbyname("http", "tcp");
	if(!servinfo)
		return 3;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (sockfd == -1)
		return 4;
	address.sin_family = AF_INET;
	address.sin_port = servinfo->s_port;
	address.sin_addr = *(struct in_addr *)*hostinfo->h_addr_list;

	result = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
	if(result == -1) {
		close(sockfd);
		return 5;
	}

	{
		detail = "GET /bin/xml?s=";
		detail += qrzSessionKey;
		detail += ";callsign=";
		detail += callsign;
		detail += " HTTP/1.0\n";
		detail += "Host: ";
		detail += host;
		detail += "\n";
		detail += "Connection: close\n";
		detail += "\n";
	}
	
	result = write(sockfd, detail.c_str() , detail.length());
	if (result != (int)detail.length()) {
		close(sockfd);
		return 6;
	}

	FD_ZERO(&readfds);
	FD_SET(sockfd, &readfds);
	
	while (1) {
		testfds = readfds;
		timeout.tv_sec = 5;		// timeout = 5 seconds
		timeout.tv_usec = 0;
		result = select(FD_SETSIZE, &testfds, (fd_set *)0, (fd_set *)0, &timeout);
		if (result == 0) {
			break;
		}
		if (result == -1) {
			close(sockfd);
			return 8;
		}
		if (FD_ISSET(sockfd, &testfds)) {
			memset(rbuffer, 0, 32768);
			result = read(sockfd, rbuffer, sizeof(rbuffer));
			if (result <= 0) break;
			xmlpage += rbuffer;
		} else {
			break;
		}
	}
    
	close(sockfd);
	return 0;
}

int bearing(const char *myqra, const char *dxqra) {
	double	lat1, lat1r, lon1;
	double	lat2, lat2r, lon2;
	double	dlong, arg1, arg2a, arg2b, arg2, bearingr, bearing;
	double	k=180.0/M_PI;

	qra(dxqra, lat2, lon2);
	qra(myqra, lat1, lon1);

	lat1r=lat1/k;
	lat2r=lat2/k;

	dlong = lon2/k - lon1/k;

	arg1 = sin(dlong) * cos(lat2r);
	arg2a = cos(lat1r) * sin(lat2r);
	arg2b = sin(lat1r) * cos(lat2r) * cos(dlong);
	arg2 =  arg2a -  arg2b;
	bearingr = atan2(arg1, arg2);

	bearing = floor(0.5+fmod(360.0 + (bearingr * k), 360.0));
	return (int)bearing;
}

void qra(const char *szqra, double &lat, double &lon) {
	int c1 = toupper(szqra[0])-'A';
	int c2 = toupper(szqra[1])-'A';
	int c3 = szqra[2]-'0';
	int c4 = szqra[3]-'0';
	int c5, c6;
	if (strlen(szqra) > 4) {
    	c5 = toupper(szqra[4])-'A';
    	c6 = toupper(szqra[5])-'A';
		lat = (((c2 * 10.0) + c4 + ((c6 + 0.5)/24.0)) - 90.0);
		lon = (((c1 * 20.0) + (c3 * 2.0) + ((c5 + 0.5) / 12.0)) - 180.0);
	} else {
		lat = (((c2 * 10.0) + c4 ) - 90.0);
		lon = (((c1 * 20.0) + (c3 * 2.0)) - 180.0);
	}
}

void QRZ_disp_result()
{
   FL_LOCK();
   {
       if (qrzfname.length() > 0) {
           int spacePos = qrzfname.find(" ");
//    if fname is "ABC" then display "ABC"
// or if fname is "X Y" then display "X Y"
           if (spacePos ==-1 || (spacePos == 1)) {
               inpName->value(qrzfname.c_str());
           }
// if fname is "ABC Y" then display "ABC"
           else if (spacePos == ((int)qrzfname.length())-2) {
               string fname="";
               fname.assign(qrzfname, 0, spacePos);
               inpName->value(fname.c_str());
           }
// fname must be "ABC DEF" so display "ABC DEF"
           else {
               inpName->value(qrzfname.c_str());
           }
       } else if (qrzname.length() > 0) {
// only name is set; don't know first/last, so just show all
           inpName->value(qrzname.c_str());
       }
   }

   inpQth->value(qrzqth.c_str());
   inpLoc->value(qrzgrid.c_str());
   if (!progdefaults.myLocator.empty()) {
       char buf[10];
       buf[0] = '\0';
       if (!qrzgrid.empty()) {
           int b = bearing( progdefaults.myLocator.c_str(), qrzgrid.c_str() );
           b+=rotoroffset;
           if (b<0) b+=360;
           if (b>=360) b-=360;
           int br = (b + 180) % 360;
           snprintf(buf, sizeof(buf), "%03d / %03d", b, br);
       }
       inpAZ->value(buf);
   }
   inpNotes->value(qrznotes.c_str());
   FL_UNLOCK();
}

void QRZ_COM_query()
{
	if (!QRZ_enabled) {
		QRZinit();
		if (!QRZ_enabled)
			return;
	}

	xmlpage = "";
	DB_query = QRZNET;
	FL_LOCK();
	inpNotes->value(" *** Request sent to qrz.com ***");
	FL_UNLOCK();
}

void HAMCALL_COM_query()
{
	if (!QRZ_enabled) {
		QRZinit();
		if (!QRZ_enabled)
			return;
	}
	htmlpage = "";
	DB_query = HAMCALLNET;
	FL_LOCK();
	inpNotes->value(" *** Request sent to www.hamcomm.net ***");
	FL_UNLOCK();	
}

void QRZ_CD_query()
{
	char srch[20];
	size_t snip;
	
	memset( srch, 0, 20 );
	strncpy( srch, callsign.c_str(), 6 );
	for (size_t i = 0; i < strlen(srch); i ++ )
		srch[i] = toupper(srch[i]);

	if( qCall->FindRecord( srch ) == 1) {
		qrzfname = qCall->GetFname();
		snip = qrzfname.find(' ');
		if (snip != string::npos)
			qrzfname.erase(snip, qrzfname.length() - snip);
		qrzqth = qCall->GetCity();
		qrzqth.append(" ");
		qrzqth.append(qCall->GetState());
		qrzqth.append(" ");
		qrzqth.append(qCall->GetZIP());
		qrzgrid = "";
		qrznotes = "";
	} else {
		qrzfname = "";
		qrzqth = "";
		qrzgrid = "";
		qrzborn = "";
		qrznotes = "Not found in CD database!";
	}
	QRZ_disp_result();
}

void QRZinit(void)
{
	QRZ_enabled = false;
	if (fl_create_thread(QRZ_thread, CALLSIGNloop, &qrzdummy) < 0) {
		fl_message("QRZ init: pthread_create failed");
		return;
	} 
	QRZ_enabled = true;
}

void QRZclose(void)
{
	if (!QRZ_enabled) return;
// tell the QRZ thread to kill it self
	QRZ_exit = true;
// and then wait for it to die
	fl_join(QRZ_thread);
	QRZ_enabled = false;
	QRZ_exit = false;
}

void qthappend(string &qth, string &datum) {
	if (datum.empty()) return;
	if (!qth.empty()) qth += ", ";
	qth += datum;
}

void QRZAlert()
{
// test alert first as QRZ.com requires it be shown
	if (!qrzalert.empty()) {
		FL_LOCK();
		inpNotes->value(qrzalert.c_str());
		qrzalert="";
		FL_UNLOCK();
	} else if (!qrzerror.empty()) {
		FL_LOCK();
		inpNotes->value(qrzerror.c_str());
		qrzerror="";
		FL_UNLOCK();
	}
}

int QRZLogin() {
	int err=0;
	if (qrzSessionKey.empty()) {
		err = getSessionKey();
		if (!err) err = parseSessionKey();
	}
	if (! err) {
		QRZAlert();
	}
	return err;
}

void QRZquery()
{
	int err=0;
	if (qrzSessionKey.empty())
		err = QRZLogin();
	if (!err)
		err = QRZGetXML();
	if (!err) {
		if (qrzSessionKey.empty())
			err = QRZLogin();
		if (!err)
			err = QRZGetXML();
	}
	if (!err) {
		parse_xml();
		if (!qrzalert.empty()) {
			FL_LOCK();
			inpNotes->value(qrzalert.c_str());
			qrzalert="";
			FL_UNLOCK();
		} else if (!qrzerror.empty()) {
			FL_LOCK();
			inpNotes->value(qrzerror.c_str());
			qrzerror="";
			FL_UNLOCK();
		} else {
			qrzqth = "";
			qthappend(qrzqth, qrzaddr1);
			qthappend(qrzqth, qrzaddr2);
			qthappend(qrzqth, qrzstate);
			qthappend(qrzqth, qrzcountry);
			QRZ_disp_result();
		}
	} 
	if (err) {
		FL_LOCK();
		inpNotes->value(error[err]);
		FL_UNLOCK();
	}
}

// Hamcall specific functions

#define HAMCALL_HOST "www.hamcall.net"
#define HAMCALL_CALL 181
#define HAMCALL_FIRST 184
#define HAMCALL_CITY 191
#define HAMCALL_STATE 192
#define HAMCALL_GRID 202
#define HAMCALL_DOB 194

void parse_html()
{
	size_t p;
	qrzborn="";
	qrzfname="";
	qrzqth="";
	qrzgrid="";
	qrzlatd="";
	qrzlond="";
	qrznotes="";
	
	if ((p = htmlpage.find(HAMCALL_FIRST)) != string::npos) {
		p++;
		while ((uchar)htmlpage[p] < 128 && p < htmlpage.length() )
			qrzfname += htmlpage[p++];
	}
	if ((p = htmlpage.find(HAMCALL_CITY)) != string::npos) { 
		p++;
		while ((uchar)htmlpage[p] < 128 && p < htmlpage.length())
			qrzqth += htmlpage[p++];
		qrzqth += ", ";
	}
	if ((p = htmlpage.find(HAMCALL_STATE)) != string::npos) { 
		p++;
		while ((uchar)htmlpage[p] < 128 && p < htmlpage.length())
			qrzqth += htmlpage[p++];
	}
	if ((p = htmlpage.find(HAMCALL_GRID)) != string::npos) { 
		p++;
		while ((uchar)htmlpage[p] < 128 && p < htmlpage.length())
			qrzgrid += htmlpage[p++];
	}
	if ((p = htmlpage.find(HAMCALL_DOB)) != string::npos) { 
		p++;
		qrznotes = "DOB: ";
		while ((uchar)htmlpage[p] < 128 && p < htmlpage.length())
			qrznotes += htmlpage[p++];
	}	
}

int HAMCALLget()
{
	string url_detail;
	int len;
	
	url_detail = "GET /call?username=";
	url_detail += progdefaults.QRZusername;
	url_detail += "&password=";
	url_detail += progdefaults.QRZuserpassword;
	url_detail += "&rawlookup=1&callsign=";
	url_detail += callsign;
	url_detail += "&program=WA5ZNU/Testing/0.0+\r\n";

    hostinfo = gethostbyname(HAMCALL_HOST);
    if(!hostinfo)
    	return 1;
    if(hostinfo->h_addrtype != AF_INET)
    	return 2;
    servinfo = getservbyname("http", "tcp");
    if(!servinfo)
    	return 3;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
		return 4;

    address.sin_family = AF_INET;
    address.sin_port = servinfo->s_port;
    address.sin_addr = *(struct in_addr *)*hostinfo->h_addr_list;

    result = connect(sockfd, (struct sockaddr *)&address, sizeof(address));
    if(result == -1) {
		close(sockfd);
    	return 5;
	}
	
	len = url_detail.length();
	result = write(sockfd, url_detail.c_str() , len);
	if (result != len) {
		close(sockfd);
		return 6;
	}

	FD_ZERO(&readfds);
	FD_SET(sockfd, &readfds);
	
	testfds = readfds;
	timeout.tv_sec = 15;		// timeout = 10 seconds
	timeout.tv_usec = 0;
	result = select(FD_SETSIZE, &testfds, (fd_set *)0, (fd_set *)0, &timeout);

	if (result == 0) {
		close(sockfd);
		return 7;
	}
	if (result == -1) {
		close(sockfd);
		return 8;
	}
	if (FD_ISSET(sockfd, &testfds)) {
		memset(rbuffer, 0, 32768);
		result = read(sockfd, rbuffer, sizeof(rbuffer));
		if (result && result < 32768)
			htmlpage += rbuffer;
	}
    
    close(sockfd);
	return 0;          
}

void HAMCALLquery()
{
int err;
	if ((err = HAMCALLget()) == 0) {
		parse_html();
		QRZ_disp_result();
	} else {
		FL_LOCK();
		inpNotes->value(error[err]);
		FL_UNLOCK();
	}
}

static void *CALLSIGNloop(void *args)
{
	SET_THREAD_ID(QRZ_TID);

	for (;;) {
// see if this thread has been canceled
		if (QRZ_exit)
			break;
		switch (DB_query) {
			case QRZCD :
				DB_query = NONE;
				break;
			case QRZNET :
				QRZquery();
				DB_query = NONE;
				break;
			case HAMCALLNET :
				HAMCALLquery();
				DB_query = NONE;
				break;
			case NONE:
			default :
				break;
		}
		MilliSleep(100);
	}
	return NULL;
}

void CALLSIGNquery()
{
	{
		FL_LOCK();
		callsign = inpCall->value();
// Filter callsign for nonesense characters (remove all but [A-Z0-9/])
		string ncall = "";
		for (unsigned int i = 0; i < callsign.length(); i++) {
			const char ch = callsign.at(i);
			if ((ch >= 'A' && ch <= 'Z') ||
			    (ch >= 'a' && ch <= 'z') ||
			    (ch >= '0' && ch <= '9') ||
			    (ch == '/')) {
				ncall += (ch);
			}
		}
		inpCall->value(ncall.c_str());
		callsign = inpCall->value();
		FL_UNLOCK();
	}
	
	if (callsign.length() == 0)
		return;
	switch (progdefaults.QRZ) {
		case 1 :
			QRZ_COM_query();
			break;
		case 2 :
			if (!qCall)
				qCall = new QRZ( "callbkc" );
			if (qCall && qCall->getQRZvalid())
				QRZ_CD_query();
			DB_query = NONE;
			break;
		case 3:
			HAMCALL_COM_query();
			break;
		case 0:
		default :
			break;
	}			
}	

/* ---------------------------------------------------------------------- */

