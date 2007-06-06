#include "stdafx.h"
#include "winsock2.h"
#include "Form1.h"
#include "StructuresForm.h"
#include "EPSForm.h"
#include "ELSForm.h"
#include "ECSForm.h"
#include "GNCForm.h"
#include "SCSForm.h"
#include "SPSForm.h"
#include "TelecomForm.h"
#include "CrewForm.h"
#include "CMCForm.h"
#include "UplinkForm.h"
#include <windows.h>
#include <stdio.h>

using namespace GroundStation;

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	System::Threading::Thread::CurrentThread->ApartmentState = System::Threading::ApartmentState::STA;
	Application::Run(new Form1());
	return 0;
}

void Form1::WinsockInit(){
	// Do this here too
	// eps_form = NULL;

	int iResult = WSAStartup( MAKEWORD(2,2), &wsaData );
	if ( iResult != NO_ERROR ){
		StatusBox->Text = "Error at WSAStartup()";
		return;
	}else{
		StatusBox->Text = "Disconnected, Winsock Ready";
	}
};

void Form1::ConnectToHost(){
	char msg[256];
	char addr[256];

	if(conn_status == 0){
		sprintf(msg,"Connecting...");
		StatusBox->Text = msg;
		m_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		if ( m_socket == INVALID_SOCKET ) {
			sprintf(msg,"Error at socket(): %ld", WSAGetLastError() );
			StatusBox->Text = msg;
			closesocket(m_socket);
			return;
		}
		// Address to ANSI string
		sprintf(addr,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(HostAddrBox->Text));

		// Now connect
		clientService.sin_family = AF_INET;
		clientService.sin_addr.s_addr = inet_addr(addr);
		clientService.sin_port = htons( 14242 );

		if ( connect( m_socket, (SOCKADDR*) &clientService, sizeof(clientService) ) == SOCKET_ERROR) {
			sprintf(msg,"Failed to connect, Error %ld",WSAGetLastError());
			closesocket(m_socket);
			StatusBox->Text = msg;
			return;
		}else{
			StatusBox->Text = "LOCK/OUT FRAM/000 WORD/000";
			// Kick service thread
			ThreadStart *myThreadDelegate = new ThreadStart(this, ThreadTask);
			trd = new Thread(myThreadDelegate);
			trd->IsBackground = true;
			trd->Start();
			ConnectBtn->Text = "Disconnect";
			conn_status = 1;
		}
	}else{
		sprintf(msg,"Disconnecting...");
		StatusBox->Text = msg;
		trd->Abort(); // Kill other thread
		conn_status = 0;
		shutdown(m_socket,SD_BOTH);
		closesocket(m_socket);
		ConnectBtn->Text = "Connect";
	}
};

// Control disabling
void Form1::end_lbr(){
	if(eps_form != NULL){
		eps_form->s11A91->Enabled = FALSE;
		eps_form->s11A109->Enabled = FALSE;
		eps_form->s11A93->Enabled = FALSE;
	}
}

void Form1::end_hbr(){
	if(eps_form != NULL){
		eps_form->s11A91->Enabled = FALSE;
		eps_form->s11A109->Enabled = FALSE;
		eps_form->s11A93->Enabled = FALSE;
	}
}

// Unscale PCM data to original value
double Form1::unscale_data(unsigned char data,double low,double high){
	double step = 0;

	if(data == 0){    return low;  }
	if(data == 0xFF){ return high; }
	step = ((high - low)/256);
	return (data*step);
}

// HBR datastream parsing
void Form1::parse_hbr(unsigned char data, int bytect){
	char msg[256];
	double value;
	switch(bytect){
		case 0: // SYNC 1
			if(data != 05){ end_hbr(); lock_type = 0; cmc_lock_type = 0; }
			break;
		case 1: // SYNC 2
			if(data != 0171){ end_hbr(); lock_type = 0; cmc_lock_type = 0; }
			break;
		case 2: // SYNC 3
			if(data != 0267){ end_hbr(); lock_type = 0; cmc_lock_type = 0; }
			break;
		case 3: // FRAME COUNT
			framect = data&077; // 0-49 frame count
			// GENERATE 0-4 SUBFRAME #
			sprintf(msg,"%03d",framect);
			switch(msg[2]){
				case '0': 
				case '5': 
					framead = 0; break;
				case '1': 
				case '6': 
					framead = 1; break;
				case '2': 
				case '7': 
					framead = 2; break;
				case '3': 
				case '8': 
					framead = 3; break;
				case '4': 
				case '9': 
					framead = 4; break;
			}
			break;		

		case 8:
			switch(framead){
				case 3:
				if(eps_form != NULL){
					value = unscale_data(data,0,100);
					sprintf(msg,"%+05.2f A",value);
					eps_form->s11A109->Enabled = TRUE;
					eps_form->s11A109->Text = msg;						
				}
				break;
			}
			break;

		case 31: // CMC DATA WORD
			cmc_w0 = data&0177;
			cmc_w0 <<= 8;
			break;
		case 32: // CMC DATA WORD
			cmc_w0 |= data&0377;
			break;
		case 33: // CMC DATA WORD
			cmc_w1 = data&0177;
			cmc_w1 <<= 8;
			break;
		case 34: // CMC DATA WORD
			cmc_w1 |= data&0377;
			break;
		case 35: // CMC DATA WORD
			parse_cmc();
			break;

		case 72:
			switch(framead){
			case 2:	
				if(eps_form != NULL){
					value = unscale_data(data,0,45);
					sprintf(msg,"%+04.2f V",value);
					eps_form->s11A91->Enabled = TRUE;
					eps_form->s11A91->Text = msg;						
				}
				break;
			}
			break;

		case 74:
			switch(framead){
			case 2:
				if(eps_form != NULL){
					value = unscale_data(data,0,45);
					sprintf(msg,"%+04.2f V",value);
					eps_form->s11A93->Enabled = TRUE;
					eps_form->s11A93->Text = msg;						
				}
				break;
			}
			break;

	}
}

// Enable/disable stuff in CMCForm and setup for the given list
void Form1::setup_cmc_list(){
	if(cmc_form == NULL){ return; } // Safeguard
	bool onoff;
	if(cmc_lock_type != 0){ onoff = TRUE; }else{ onoff = FALSE; }

	cmc_form->cmcListID->Enabled = onoff;

	cmc_form->textBox1->Enabled = onoff;
	cmc_form->textBox2->Enabled = onoff;
	cmc_form->textBox3->Enabled = onoff;
	cmc_form->textBox4->Enabled = onoff;
	cmc_form->textBox5->Enabled = onoff;
	cmc_form->textBox6->Enabled = onoff;
	cmc_form->textBox7->Enabled = onoff;
	cmc_form->textBox8->Enabled = onoff;
	cmc_form->textBox9->Enabled = onoff;

	cmc_form->textBox10->Enabled = onoff;
	cmc_form->textBox11->Enabled = onoff;
	cmc_form->textBox12->Enabled = onoff;
	cmc_form->textBox13->Enabled = onoff;
	cmc_form->textBox14->Enabled = onoff;
	cmc_form->textBox15->Enabled = onoff;
	cmc_form->textBox16->Enabled = onoff;
	cmc_form->textBox17->Enabled = onoff;
	cmc_form->textBox18->Enabled = onoff;
	cmc_form->textBox19->Enabled = onoff;

	cmc_form->textBox20->Enabled = onoff;
	cmc_form->textBox21->Enabled = onoff;
	cmc_form->textBox22->Enabled = onoff;
	cmc_form->textBox23->Enabled = onoff;
	cmc_form->textBox24->Enabled = onoff;
	cmc_form->textBox25->Enabled = onoff;
	cmc_form->textBox26->Enabled = onoff;
	cmc_form->textBox27->Enabled = onoff;
	cmc_form->textBox28->Enabled = onoff;
	cmc_form->textBox29->Enabled = onoff;

	cmc_form->textBox30->Enabled = onoff;
	cmc_form->textBox31->Enabled = onoff;
	cmc_form->textBox32->Enabled = onoff;
	cmc_form->textBox33->Enabled = onoff;
	cmc_form->textBox34->Enabled = onoff;
	cmc_form->textBox35->Enabled = onoff;
	cmc_form->textBox36->Enabled = onoff;
	cmc_form->textBox37->Enabled = onoff;
	cmc_form->textBox38->Enabled = onoff;
	cmc_form->textBox39->Enabled = onoff;

	cmc_form->textBox40->Enabled = onoff;
	cmc_form->textBox41->Enabled = onoff;
	cmc_form->textBox42->Enabled = onoff;
	cmc_form->textBox43->Enabled = onoff;
	cmc_form->textBox44->Enabled = onoff;
	cmc_form->textBox45->Enabled = onoff;
	cmc_form->textBox46->Enabled = onoff;
	cmc_form->textBox47->Enabled = onoff;
	cmc_form->textBox48->Enabled = onoff;
	cmc_form->textBox49->Enabled = onoff;

	cmc_form->textBox50->Enabled = onoff;
	cmc_form->textBox51->Enabled = onoff;
	cmc_form->textBox52->Enabled = onoff;
	cmc_form->textBox53->Enabled = onoff;
	cmc_form->textBox54->Enabled = onoff;
	cmc_form->textBox55->Enabled = onoff;
	cmc_form->textBox56->Enabled = onoff;
	cmc_form->textBox57->Enabled = onoff;
	cmc_form->textBox58->Enabled = onoff;
	cmc_form->textBox59->Enabled = onoff;

	cmc_form->textBox60->Enabled = onoff;
	cmc_form->textBox61->Enabled = onoff;
	cmc_form->textBox62->Enabled = onoff;
	cmc_form->textBox63->Enabled = onoff;
	cmc_form->textBox64->Enabled = onoff;
	cmc_form->textBox65->Enabled = onoff;
	cmc_form->textBox66->Enabled = onoff;
	cmc_form->textBox67->Enabled = onoff;
	cmc_form->textBox68->Enabled = onoff;
	cmc_form->textBox69->Enabled = onoff;

	cmc_form->textBox70->Enabled = onoff;
	cmc_form->textBox71->Enabled = onoff;
	cmc_form->textBox72->Enabled = onoff;
	cmc_form->textBox73->Enabled = onoff;
	cmc_form->textBox74->Enabled = onoff;
	cmc_form->textBox75->Enabled = onoff;
	cmc_form->textBox76->Enabled = onoff;
	cmc_form->textBox77->Enabled = onoff;
	cmc_form->textBox78->Enabled = onoff;
	cmc_form->textBox79->Enabled = onoff;

	cmc_form->textBox80->Enabled = onoff;
	cmc_form->textBox81->Enabled = onoff;
	cmc_form->textBox82->Enabled = onoff;
	cmc_form->textBox83->Enabled = onoff;
	cmc_form->textBox84->Enabled = onoff;
	cmc_form->textBox85->Enabled = onoff;
	cmc_form->textBox86->Enabled = onoff;
	cmc_form->textBox87->Enabled = onoff;
	cmc_form->textBox88->Enabled = onoff;
	cmc_form->textBox89->Enabled = onoff;

	cmc_form->textBox90->Enabled = onoff;
	cmc_form->textBox91->Enabled = onoff;
	cmc_form->textBox92->Enabled = onoff;
	cmc_form->textBox93->Enabled = onoff;
	cmc_form->textBox94->Enabled = onoff;
	cmc_form->textBox95->Enabled = onoff;
	cmc_form->textBox96->Enabled = onoff;
	cmc_form->textBox97->Enabled = onoff;
	cmc_form->textBox98->Enabled = onoff;
	cmc_form->textBox99->Enabled = onoff;

	cmc_form->textBox100->Enabled = onoff;
	cmc_form->textBox101->Enabled = onoff;
	cmc_form->textBox102->Enabled = onoff;
	cmc_form->textBox103->Enabled = onoff;
	cmc_form->textBox104->Enabled = onoff;
	cmc_form->textBox105->Enabled = onoff;
	cmc_form->textBox106->Enabled = onoff;
	cmc_form->textBox107->Enabled = onoff;
	cmc_form->textBox108->Enabled = onoff;
	cmc_form->textBox109->Enabled = onoff;

	cmc_form->textBox110->Enabled = onoff;
	cmc_form->textBox111->Enabled = onoff;
	cmc_form->textBox112->Enabled = onoff;
	cmc_form->textBox113->Enabled = onoff;
	cmc_form->textBox114->Enabled = onoff;
	cmc_form->textBox115->Enabled = onoff;
	cmc_form->textBox116->Enabled = onoff;
	cmc_form->textBox117->Enabled = onoff;
	cmc_form->textBox118->Enabled = onoff;
	cmc_form->textBox119->Enabled = onoff;

	cmc_form->textBox120->Enabled = onoff;
	cmc_form->textBox121->Enabled = onoff;
	cmc_form->textBox122->Enabled = onoff;
	cmc_form->textBox123->Enabled = onoff;
	cmc_form->textBox124->Enabled = onoff;
	cmc_form->textBox125->Enabled = onoff;
	cmc_form->textBox126->Enabled = onoff;
	cmc_form->textBox127->Enabled = onoff;
	cmc_form->textBox128->Enabled = onoff;
	cmc_form->textBox129->Enabled = onoff;

	cmc_form->textBox130->Enabled = onoff;
	cmc_form->textBox131->Enabled = onoff;
	cmc_form->textBox132->Enabled = onoff;
	cmc_form->textBox133->Enabled = onoff;
	cmc_form->textBox134->Enabled = onoff;
	cmc_form->textBox135->Enabled = onoff;
	cmc_form->textBox136->Enabled = onoff;
	cmc_form->textBox137->Enabled = onoff;
	cmc_form->textBox138->Enabled = onoff;
	cmc_form->textBox139->Enabled = onoff;

	cmc_form->textBox140->Enabled = onoff;
	cmc_form->textBox141->Enabled = onoff;
	cmc_form->textBox142->Enabled = onoff;
	cmc_form->textBox143->Enabled = onoff;
	cmc_form->textBox144->Enabled = onoff;
	cmc_form->textBox145->Enabled = onoff;
	cmc_form->textBox146->Enabled = onoff;
	cmc_form->textBox147->Enabled = onoff;
	cmc_form->textBox148->Enabled = onoff;
	cmc_form->textBox149->Enabled = onoff;

	cmc_form->textBox150->Enabled = onoff;
	cmc_form->textBox151->Enabled = onoff;
	cmc_form->textBox152->Enabled = onoff;
	cmc_form->textBox153->Enabled = onoff;
	cmc_form->textBox154->Enabled = onoff;
	cmc_form->textBox155->Enabled = onoff;
	cmc_form->textBox156->Enabled = onoff;
	cmc_form->textBox157->Enabled = onoff;
	cmc_form->textBox158->Enabled = onoff;
	cmc_form->textBox159->Enabled = onoff;

	cmc_form->textBox160->Enabled = onoff;
	cmc_form->textBox161->Enabled = onoff;
	cmc_form->textBox162->Enabled = onoff;
	cmc_form->textBox163->Enabled = onoff;
	cmc_form->textBox164->Enabled = onoff;
	cmc_form->textBox165->Enabled = onoff;
	cmc_form->textBox166->Enabled = onoff;
	cmc_form->textBox167->Enabled = onoff;
	cmc_form->textBox168->Enabled = onoff;
	cmc_form->textBox169->Enabled = onoff;

	cmc_form->textBox170->Enabled = onoff;
	cmc_form->textBox171->Enabled = onoff;
	cmc_form->textBox172->Enabled = onoff;
	cmc_form->textBox173->Enabled = onoff;
	cmc_form->textBox174->Enabled = onoff;
	cmc_form->textBox175->Enabled = onoff;
	cmc_form->textBox176->Enabled = onoff;
	cmc_form->textBox177->Enabled = onoff;
	cmc_form->textBox178->Enabled = onoff;
	cmc_form->textBox179->Enabled = onoff;
}

void Form1::parse_cmc(){
	char msg[256];
	// All CMC data lists are 200 registers long.

	if(cmc_form == NULL){ return; } // Don't waste time with this if we aren't reading the output.

	if(cmc_lock_type == 0){
		cmc_frame_addr = 1;   // Hold at one
		if(cmc_w1 == 077340){ // Check for SYNC 1
				switch(cmc_w0){   // Switch other halfword
					case 077777:  // COAST AND ALIGN 
						cmc_lock_type = 1; 
						cmc_form->cmcListID->Text = "COAST/ALIGN";
						setup_cmc_list();
						break;
					case 077776:  // ENTRY AND UPDATE
						cmc_lock_type = 2; 
						cmc_form->cmcListID->Text = "ENTRY/UPDATE"; 
						setup_cmc_list();
						break;
					case 077775:  // RDZ AND PRETHRUST
						cmc_lock_type = 3;
						cmc_form->cmcListID->Text = "RDZ/PRETHRUST";
						setup_cmc_list();
						break;
					case 077774:  // POWERED LIST
						cmc_lock_type = 4;
						cmc_form->cmcListID->Text = "POWERED"; 
						setup_cmc_list();
						break;
					case 077773:  // ORBITAL NAV
						cmc_lock_type = 5;
						cmc_form->cmcListID->Text = "ORBITAL NAV"; 
						setup_cmc_list();
						break;
					default:
						cmc_lock_type = 0;
						cmc_form->cmcListID->Text = "NO SYNC"; 
						break;
				}
			}else{
				cmc_lock_type = 0;
				cmc_form->cmcListID->Text = "NO SYNC"; 
			}
			return;
	}else{
		cmc_frame_addr++;
		if(cmc_frame_addr > 100){
			cmc_frame_addr = 1; // LOOP
		}
	}
	// ACTUAL DATA PARSING HERE
	switch(cmc_frame_addr){
		case 1: // SYNC WORDS
			if(cmc_w1 == 077340){ // Check for SYNC 1
				switch(cmc_w0){   // Switch other halfword
					case 077777:  // COAST AND ALIGN 
						cmc_lock_type = 1; 
						cmc_form->cmcListID->Text = "COAST/ALIGN"; 
						setup_cmc_list();
						break;
					case 077776:  // ENTRY AND UPDATE
						cmc_lock_type = 2; 
						cmc_form->cmcListID->Text = "ENTRY/UPDATE"; 
						setup_cmc_list();
						break;
					case 077775:  // RDZ AND PRETHRUST
						cmc_lock_type = 3;
						cmc_form->cmcListID->Text = "RDZ/PRETHRUST"; 
						setup_cmc_list();
						break;
					case 077774:  // POWERED LIST
						cmc_lock_type = 4;
						cmc_form->cmcListID->Text = "POWERED"; 
						setup_cmc_list();
						break;
					case 077773:  // ORBITAL NAV
						cmc_lock_type = 5;
						cmc_form->cmcListID->Text = "ORBITAL NAV"; 
						setup_cmc_list();
						break;
					default:
						cmc_lock_type = 0;
						cmc_form->cmcListID->Text = "NO SYNC"; 
						break;
				}
			}else{
				cmc_lock_type = 0;
				cmc_form->cmcListID->Text = "NO SYNC";
			}
			break;
		// CSM STATE VECTOR
		case 2: 
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox1->Text = msg; break;
		case 3:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox2->Text = msg; break;
		case 4:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox3->Text = msg; break;
		case 5:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox4->Text = msg; break;
		case 6:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox5->Text = msg; break;
		case 7:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox6->Text = msg; break;
		case 8:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox7->Text = msg; break;
		// CDU INPUT COUNTERS X,Y,Z,TRUN
		case 9:
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox10->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox9->Text = msg;
			break;
		case 10:
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox8->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox11->Text = msg;
			break;
		// ADOT X-Y-Z
		case 11:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox14->Text = msg; break;
		case 12:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox13->Text = msg; break;
		case 13:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox15->Text = msg; break;
		// AK (FDAI ERR) and RCSFLAGS
		case 14:
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox17->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox16->Text = msg;
			break;
		case 15:
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox18->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox19->Text = msg;
			break;
		// THETADx/y/z and DELCDUx
		case 16:
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox21->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox20->Text = msg;
			break;
		case 17:
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox22->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox23->Text = msg;
			break;
		// TIG               (LIST 1,2,3)
		/* ****************** LIST 4 SVMRKDAT START ***************** */
		// CMDAPMOD and PREL (LIST 5)
		case 18:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox24->Text = msg;
					cmc_form->textBox160->Text = "XXXXX";
					cmc_form->textBox163->Text = "XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox160->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox163->Text = msg;
					cmc_form->textBox24->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// BESTI and BESTJ (LIST 1)
		// DELLT4          (LIST 2,3)
		// QREL and RREL   (LIST 5)
		case 19:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox26->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox25->Text = msg;
					cmc_form->textBox65->Text = "XXXXX-XXXXX";
					cmc_form->textBox162->Text = "XXXXX";
					cmc_form->textBox161->Text = "XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox162->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox161->Text = msg;
					cmc_form->textBox65->Text = "XXXXX-XXXXX";
					cmc_form->textBox26->Text = "XXXXX";
					cmc_form->textBox25->Text = "XXXXX";
					break;
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox65->Text = msg; 
					cmc_form->textBox26->Text = "XXXXX";
					cmc_form->textBox25->Text = "XXXXX";
					cmc_form->textBox162->Text = "XXXXX";
					cmc_form->textBox161->Text = "XXXXX";
					break;
			}
			break;
		// MARKDOWN and BDT       (LIST 1)
		// RTARGx/y/z and VHFTIME (LIST 2)
		// RTARGx/y/z and TGO     (LIST 3)
		// LD1                    (LIST 5)
		case 20:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox27->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox28->Text = msg;
					cmc_form->textBox132->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox164->Text = msg; 
					cmc_form->textBox132->Text = "XXXXX-XXXXX";
					cmc_form->textBox27->Text = "XXXXX";
					cmc_form->textBox28->Text = "XXXXX";
					break;
				case 3: 
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox132->Text = msg; 
					break;
			}
			break;
		/* ****************** LIST 5 UPBUFF START ******************* */
		case 21:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox31->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox30->Text = msg;
					cmc_form->textBox131->Text = "XXXXX-XXXXX";
					break;
				case 2:
					cmc_form->textBox30->Text = "XXXXX";
					cmc_form->textBox31->Text = "XXXXX";
					cmc_form->textBox131->Text = "XXXXX-XXXXX";
					cmc_upbuff[0] = cmc_w0; cmc_upbuff[1] = cmc_w1;
					break;
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox131->Text = msg; 
					break;
			}
			break;
		case 22:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox33->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox29->Text = msg;
					cmc_form->textBox130->Text = "XXXXX-XXXXX";
					break;
				case 2:
					cmc_form->textBox33->Text = "XXXXX";
					cmc_form->textBox29->Text = "XXXXX";
					cmc_form->textBox130->Text = "XXXXX-XXXXX";
					cmc_upbuff[2] = cmc_w0; cmc_upbuff[3] = cmc_w1;
					break;
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox130->Text = msg; 
					break;
			}
			break;
		case 23:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox32->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox34->Text = msg;
					cmc_form->textBox66->Text = "XXXXX-XXXXX";
					cmc_form->textBox152->Text = "XXXXX-XXXXX";
					break;
				case 2:
					cmc_form->textBox32->Text = "XXXXX";
					cmc_form->textBox34->Text = "XXXXX";
					cmc_form->textBox152->Text = "XXXXX-XXXXX";
					cmc_form->textBox66->Text = "XXXXX-XXXXX";
					cmc_upbuff[4] = cmc_w0; cmc_upbuff[5] = cmc_w1;
					break;
				case 3: 
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox66->Text = msg; 
					cmc_form->textBox152->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox152->Text = msg; 
					cmc_form->textBox66->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// MARK2DWN and UNITV+2  (LIST 1)
		// MARKDOWN and RM       (LIST 2)
		// PIPTIME and DELVx/y/z (LIST 3)
		case 24:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox36->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox35->Text = msg;
					cmc_form->textBox153->Text = "XXXXX-XXXXX";
					break;
				case 2:
					cmc_form->textBox36->Text = "XXXXX";
					cmc_form->textBox35->Text = "XXXXX";
					cmc_form->textBox27->Text = "XXXXX";
					cmc_form->textBox28->Text = "XXXXX";
					//cmc_form->textBox153->Text = "XXXXX-XXXXX";
					cmc_upbuff[6] = cmc_w0; cmc_upbuff[7] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox27->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox28->Text = msg;
					cmc_form->textBox36->Text = "XXXXX";
					cmc_form->textBox35->Text = "XXXXX";
					cmc_form->textBox153->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox153->Text = msg; 
					cmc_form->textBox36->Text = "XXXXX";
					cmc_form->textBox35->Text = "XXXXX";					
					cmc_form->textBox27->Text = "XXXXX";
					cmc_form->textBox28->Text = "XXXXX";					
					break;
			}
			break;
		case 25:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox39->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox41->Text = msg;
					break;
				case 2:
					cmc_form->textBox39->Text = "XXXXX";
					cmc_form->textBox41->Text = "XXXXX";
					cmc_form->textBox31->Text = "XXXXX";
					cmc_form->textBox30->Text = "XXXXX";
					cmc_form->textBox138->Text = "XXXXX-XXXXX";
					cmc_upbuff[8] = cmc_w0; cmc_upbuff[9] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox31->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox30->Text = msg;
					cmc_form->textBox39->Text = "XXXXX";
					cmc_form->textBox41->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox138->Text = msg; 
					cmc_form->textBox39->Text = "XXXXX";
					cmc_form->textBox41->Text = "XXXXX";					
					cmc_form->textBox31->Text = "XXXXX";
					cmc_form->textBox30->Text = "XXXXX";					
					break;
			}
			break;
		case 26:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox37->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox40->Text = msg;
					break;
				case 2:
					cmc_form->textBox37->Text = "XXXXX";
					cmc_form->textBox40->Text = "XXXXX";
					cmc_form->textBox33->Text = "XXXXX";
					cmc_form->textBox29->Text = "XXXXX";
					cmc_form->textBox76->Text = "XXXXX-XXXXX";
					cmc_upbuff[10] = cmc_w0; cmc_upbuff[11] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox33->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox29->Text = msg;
					cmc_form->textBox37->Text = "XXXXX";
					cmc_form->textBox40->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox76->Text = msg; 
					cmc_form->textBox37->Text = "XXXXX";
					cmc_form->textBox40->Text = "XXXXX";					
					cmc_form->textBox33->Text = "XXXXX";
					cmc_form->textBox29->Text = "XXXXX";					
					break;
			}
			break;
		case 27:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox38->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox42->Text = msg;
					cmc_form->textBox67->Text = "XXXXX";
					break;
				case 2:
					cmc_form->textBox38->Text = "XXXXX";
					cmc_form->textBox42->Text = "XXXXX";
					cmc_form->textBox32->Text = "XXXXX";
					cmc_form->textBox67->Text = "XXXXX";
					cmc_form->textBox75->Text = "XXXXX-XXXXX";
					cmc_upbuff[12] = cmc_w0; cmc_upbuff[13] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox32->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox67->Text = msg;
					cmc_form->textBox38->Text = "XXXXX";
					cmc_form->textBox42->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox75->Text = msg; 
					cmc_form->textBox38->Text = "XXXXX";
					cmc_form->textBox42->Text = "XXXXX";					
					cmc_form->textBox32->Text = "XXXXX";
					cmc_form->textBox67->Text = "XXXXX";					
					break;
			}
			break;
		// HAPO and HPER             (LIST 1)
		// VHFCNT,TRKMCNT and TTPI   (LIST 2)
		// PACTOFF,YACTOFF,PCMD,YCMD (LIST 3)
		case 28:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox44->Text = msg;
					cmc_form->textBox70->Text = "XXXXX";
					cmc_form->textBox69->Text = "XXXXX";
					cmc_form->textBox156->Text = "XXXXX";
					cmc_form->textBox155->Text = "XXXXX";
					break;
				case 2:
					cmc_form->textBox70->Text = "XXXXX";
					cmc_form->textBox69->Text = "XXXXX";
					cmc_form->textBox156->Text = "XXXXX";
					cmc_form->textBox155->Text = "XXXXX";
					cmc_form->textBox44->Text = "XXXXX-XXXXX";
					cmc_upbuff[14] = cmc_w0; cmc_upbuff[15] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox70->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox69->Text = msg;
					cmc_form->textBox44->Text = "XXXXX-XXXXX";
					cmc_form->textBox156->Text = "XXXXX";
					cmc_form->textBox155->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox156->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox155->Text = msg;
					cmc_form->textBox44->Text = "XXXXX-XXXXX";
					cmc_form->textBox70->Text = "XXXXX";
					cmc_form->textBox69->Text = "XXXXX";
					break;
			}
			break;
		case 29:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox43->Text = msg;
					cmc_form->textBox68->Text = "XXXXX-XXXXX";
					cmc_form->textBox154->Text = "XXXXX";
					cmc_form->textBox157->Text = "XXXXX";
					break;
				case 2:
					cmc_form->textBox154->Text = "XXXXX";
					cmc_form->textBox157->Text = "XXXXX";
					cmc_form->textBox43->Text = "XXXXX-XXXXX";
					cmc_form->textBox68->Text = "XXXXX-XXXXX";
					cmc_upbuff[16] = cmc_w0; cmc_upbuff[17] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox68->Text = msg;
					cmc_form->textBox43->Text = "XXXXX-XXXXX";
					cmc_form->textBox154->Text = "XXXXX";
					cmc_form->textBox157->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox154->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox157->Text = msg;
					cmc_form->textBox43->Text = "XXXXX-XXXXX";
					cmc_form->textBox68->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// RSP-RREC         (LIST 1)
		// ECSTEER and LSAT (LIST 2)
		// CSTEER and ALP   (LIST 3)
		/* ****************** LIST 5 UPBUFF ENDS ******************** */
		case 30:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox45->Text = msg;
					cmc_form->textBox71->Text = "XXXXX";
					cmc_form->textBox72->Text = "XXXXX";
					cmc_form->textBox159->Text = "XXXXX";
					cmc_form->textBox158->Text = "XXXXX";
					break;
				case 2:
					cmc_form->textBox71->Text = "XXXXX";
					cmc_form->textBox72->Text = "XXXXX";
					cmc_form->textBox159->Text = "XXXXX";
					cmc_form->textBox158->Text = "XXXXX";
					cmc_form->textBox45->Text = "XXXXX-XXXXX";
					cmc_upbuff[18] = cmc_w0; cmc_upbuff[19] = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox71->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox72->Text = msg;
					cmc_form->textBox45->Text = "XXXXX-XXXXX";
					cmc_form->textBox159->Text = "XXXXX";
					cmc_form->textBox158->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox159->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox158->Text = msg;
					cmc_form->textBox45->Text = "XXXXX-XXXXX";
					cmc_form->textBox71->Text = "XXXXX";
					cmc_form->textBox72->Text = "XXXXX";
					break;
			}
			break;
		// VGTIGx/y/z         (LIST 1)
		// DELVTPF and SPARES (LIST 2)
		// SPARES             (LIST 3)
		// COMPNUMB,UPOLDMOD  (LIST 5)
		case 31:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox48->Text = msg;
					cmc_form->textBox73->Text = "XXXXX-XXXXX";
					break;
				case 2:
					cmc_form->textBox73->Text = "XXXXX-XXXXX";
					cmc_compnumb = cmc_w0; cmc_upoldmod = cmc_w1;
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox73->Text = msg;
					cmc_form->textBox48->Text = "XXXXX-XXXXX";
					break;
				case 4:
					cmc_form->textBox73->Text = "XXXXX-XXXXX";					
					break;
			}
			break;
		// UPVERB,UPCOUNT     (LIST 5)
		case 32:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox47->Text = msg;
					break;
				case 2:
					cmc_form->textBox47->Text = "XXXXX-XXXXX";
					cmc_upverb = cmc_w0; cmc_upcount = cmc_w1;
					break;
				case 3:
					cmc_form->textBox47->Text = "XXXXX-XXXXX";
					break;
				case 4:
					break;
			}
			// Kick uplink processor
			cmc_uplink_process();
			break;
		// PAXERRI,ROLLTM     (LIST 5)
		case 33:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox46->Text = msg;
					cmc_form->textBox165->Text = "XXXXX";
					cmc_form->textBox166->Text = "XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox166->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox165->Text = msg;
					cmc_form->textBox46->Text = "XXXXX-XXXXX";
					break;
				case 3:
					cmc_form->textBox46->Text = "XXXXX-XXXXX";
					cmc_form->textBox166->Text = "XXXXX";
					cmc_form->textBox165->Text = "XXXXX";
					break;
				case 4:
					break;
			}
			break;
		// REFSMMAT                            (LIST 1,3)
		// TPASS4,DELVSLVx/y/z,RANGE and RRATE (LIST 2)
		// LATANG                              (LIST 5)
		case 34:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox49->Text = msg;
					cmc_form->textBox74->Text = "XXXXX-XXXXX";
					cmc_form->textBox169->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox169->Text = msg;
					cmc_form->textBox74->Text = "XXXXX-XXXXX";
					cmc_form->textBox49->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox74->Text = msg;
					cmc_form->textBox169->Text = "XXXXX-XXXXX";
					cmc_form->textBox49->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox49->Text = msg;
					break;
			}
			break;
		/* ****************** LIST 4 SVMRKDAT ENDS ****************** */
		// RDOT                                (LIST 5)
		case 35:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox50->Text = msg;
					cmc_form->textBox138->Text = "XXXXX-XXXXX";
					cmc_form->textBox168->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox168->Text = msg;
					cmc_form->textBox50->Text = "XXXXX-XXXXX";
					cmc_form->textBox138->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox138->Text = msg;
					cmc_form->textBox50->Text = "XXXXX-XXXXX";
					cmc_form->textBox168->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox50->Text = msg;
					cmc_form->textBox168->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// LANDMARK and ECC (LIST 4)
		// THETAH           (LIST 5)
		case 36:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox51->Text = msg;
					cmc_form->textBox76->Text = "XXXXX-XXXXX";
					cmc_form->textBox167->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox167->Text = msg;
					cmc_form->textBox76->Text = "XXXXX-XXXXX";
					cmc_form->textBox51->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox76->Text = msg;
					cmc_form->textBox51->Text = "XXXXX-XXXXX";
					cmc_form->textBox167->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox51->Text = msg;
					cmc_form->textBox167->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// SPARE  (LIST 4)
		// LATSPL (LIST 5)
		case 37:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox52->Text = msg;
					cmc_form->textBox75->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox148->Text = msg;
					cmc_form->textBox75->Text = "XXXXX-XXXXX";
					cmc_form->textBox52->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox75->Text = msg;
					cmc_form->textBox52->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox52->Text = msg;
					break;
			}
			break;
		// SPARE  (LIST 4)
		// LNGSPL (LIST 5)
		case 38:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox53->Text = msg;
					cmc_form->textBox140->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox149->Text = msg;
					cmc_form->textBox53->Text = "XXXXX-XXXXX";
					cmc_form->textBox140->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox140->Text = msg;
					cmc_form->textBox53->Text = "XXXXX-XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox53->Text = msg;
					break;
			}
			break;
		// SPARE             (LIST 4)
		// ALFA/180,BETA/180 (LIST 5)
		case 39:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox54->Text = msg;
					cmc_form->textBox139->Text = "XXXXX-XXXXX";
					cmc_form->textBox170->Text = "XXXXX";
					cmc_form->textBox171->Text = "XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox171->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox170->Text = msg;
					cmc_form->textBox139->Text = "XXXXX-XXXXX";
					cmc_form->textBox54->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox139->Text = msg;
					cmc_form->textBox54->Text = "XXXXX-XXXXX";
					cmc_form->textBox170->Text = "XXXXX";
					cmc_form->textBox171->Text = "XXXXX";
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox54->Text = msg;
					cmc_form->textBox170->Text = "XXXXX";
					cmc_form->textBox171->Text = "XXXXX";
					break;
			}
			break;
		// FLAGWORDS
		case 40: // FW 0-1
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox55->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox56->Text = msg;
			break;
		case 41: // FW 2-3
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox58->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox57->Text = msg;
			break;
		case 42: // FW 4-5
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox60->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox59->Text = msg;
			break;
		case 43: // FW 6-7
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox63->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox62->Text = msg;
			break;
		case 44: // FW 8-9
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox61->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox64->Text = msg;
			break;
		// DSPTAB
		case 45:
			dsptab[0] = cmc_w0; dsptab[1] = cmc_w1;
			break;
		case 46: 
			dsptab[2] = cmc_w0; dsptab[3] = cmc_w1;
			break;
		case 47: 
			dsptab[4] = cmc_w0; dsptab[5] = cmc_w1;
			break;
		case 48: 
			dsptab[6] = cmc_w0; dsptab[7] = cmc_w1;
			break;
		case 49: 
			dsptab[8] = cmc_w0; dsptab[9] = cmc_w1;
			break;
		case 50: 
			dsptab[10] = cmc_w0; dsptab[11] = cmc_w1;			
			// PARSE DSPTAB AND CREATE DSKY DISPLAY
			char tmp[12];      // Temporary
			unsigned int bits; // Temporary
			// PROG
			bits = ((dsptab[10]&01740)>>5);
			tmp[0] = get_dsky_char(bits);
			bits = (dsptab[10]&037);
			tmp[1] = get_dsky_char(bits);
			tmp[2] = 0;
			cmc_form->textBox129->Text = tmp;
			if(upl_form != NULL){ upl_form->textBox129->Text = tmp; upl_form->textBox129->Enabled = true; }
			// VERB
			bits = ((dsptab[9]&01740)>>5);
			tmp[0] = get_dsky_char(bits);
			bits = (dsptab[9]&037);
			tmp[1] = get_dsky_char(bits);
			tmp[2] = 0;
			cmc_form->textBox133->Text = tmp;
			if(upl_form != NULL){ upl_form->textBox133->Text = tmp; upl_form->textBox133->Enabled = true;}
			// NOUN
			bits = ((dsptab[8]&01740)>>5);
			tmp[0] = get_dsky_char(bits);
			bits = (dsptab[8]&037);
			tmp[1] = get_dsky_char(bits);
			tmp[2] = 0;
			cmc_form->textBox134->Text = tmp;
			if(upl_form != NULL){ upl_form->textBox134->Text = tmp; upl_form->textBox134->Enabled = true;}
			// R1
			tmp[0] = '_';
			if(dsptab[5]&02000){
				tmp[0] = '-';
			}
			if(dsptab[6]&02000){
				tmp[0] = '+';
			}
			bits = (dsptab[7]&037);
			tmp[1] = get_dsky_char(bits);
			bits = ((dsptab[6]&01740)>>5);
			tmp[2] = get_dsky_char(bits);
			bits = (dsptab[6]&037);
			tmp[3] = get_dsky_char(bits);
			bits = ((dsptab[5]&01740)>>5);
			tmp[4] = get_dsky_char(bits);
			bits = (dsptab[5]&037);
			tmp[5] = get_dsky_char(bits);
			tmp[6] = 0;
			cmc_form->textBox135->Text = tmp;
			if(upl_form != NULL){ upl_form->textBox135->Text = tmp; upl_form->textBox135->Enabled = true;}
			// R2
			tmp[0] = '_';
			if(dsptab[3]&02000){
				tmp[0] = '-';
			}
			if(dsptab[4]&02000){
				tmp[0] = '+';
			}
			bits = ((dsptab[4]&01740)>>5);
			tmp[1] = get_dsky_char(bits);
			bits = (dsptab[4]&037);
			tmp[2] = get_dsky_char(bits);
			bits = ((dsptab[3]&01740)>>5);
			tmp[3] = get_dsky_char(bits);
			bits = (dsptab[3]&037);
			tmp[4] = get_dsky_char(bits);
			bits = ((dsptab[2]&01740)>>5);
			tmp[5] = get_dsky_char(bits);
			tmp[6] = 0;
			cmc_form->textBox136->Text = tmp;
			if(upl_form != NULL){ upl_form->textBox136->Text = tmp; upl_form->textBox136->Enabled = true;}
			// R3
			tmp[0] = '_';
			if(dsptab[0]&02000){
				tmp[0] = '-';
			}
			if(dsptab[1]&02000){
				tmp[0] = '+';
			}
			bits = (dsptab[2]&037);
			tmp[1] = get_dsky_char(bits);
			bits = ((dsptab[1]&01740)>>5);
			tmp[2] = get_dsky_char(bits);
			bits = (dsptab[1]&037);
			tmp[3] = get_dsky_char(bits);
			bits = ((dsptab[0]&01740)>>5);
			tmp[4] = get_dsky_char(bits);
			bits = (dsptab[0]&037);
			tmp[5] = get_dsky_char(bits);
			tmp[6] = 0;
			cmc_form->textBox137->Text = tmp;
			if(upl_form != NULL){ upl_form->textBox137->Text = tmp; upl_form->textBox137->Enabled = true;}
			// WARNING LIGHTS
			if(dsptab[11]&00400){ cmc_form->label72->Enabled = TRUE; }else{ cmc_form->label72->Enabled = FALSE; }
			if(dsptab[11]&00200){ cmc_form->label75->Enabled = TRUE; }else{ cmc_form->label75->Enabled = FALSE; }
			if(dsptab[11]&00040){ cmc_form->label70->Enabled = TRUE; }else{ cmc_form->label70->Enabled = FALSE; }
			if(dsptab[11]&00010){ cmc_form->label69->Enabled = TRUE; }else{ cmc_form->label69->Enabled = FALSE; }
			if(upl_form != NULL){
				if(dsptab[11]&00400){ upl_form->label72->Enabled = TRUE; }else{ upl_form->label72->Enabled = FALSE; }
				if(dsptab[11]&00200){ upl_form->label75->Enabled = TRUE; }else{ upl_form->label75->Enabled = FALSE; }
				if(dsptab[11]&00040){ upl_form->label70->Enabled = TRUE; }else{ upl_form->label70->Enabled = FALSE; }
				if(dsptab[11]&00010){ upl_form->label69->Enabled = TRUE; }else{ upl_form->label69->Enabled = FALSE; }
			}
			break;
		// TIME2 and TIME1
		case 51: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox78->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox77->Text = msg;
			break;
		// OTHER STATE VECTOR (LIST 1,2,3)
		// LAT                (LIST 4)
		// PIPTIME            (LIST 5)
		case 52: 
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox79->Text = msg;					
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox153->Text = msg;
					cmc_form->textBox79->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// LONG  (LIST 4)
		// DELVx (LIST 5)
		case 53:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox80->Text = msg;
					cmc_form->textBox174->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox174->Text = msg;
					cmc_form->textBox80->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// ALT   (LIST 4)
		// DELVy (LIST 5)
		case 54:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox81->Text = msg;
					cmc_form->textBox173->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox173->Text = msg;
					cmc_form->textBox81->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// SPARE (LIST 4)
		// DELVz (LIST 5)
		case 55:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox82->Text = msg;
					cmc_form->textBox172->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox172->Text = msg; break;
					cmc_form->textBox82->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// SPARE (LIST 4)
		// TTE   (LIST 5)
		case 56:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox83->Text = msg;
					cmc_form->textBox175->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox175->Text = msg;
					cmc_form->textBox83->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// SPARE (LIST 4)
		// VIO   (LIST 5)
		case 57:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox84->Text = msg; 
					cmc_form->textBox176->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox176->Text = msg;
					cmc_form->textBox84->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// SPARE (LIST 4)
		// VPRED (LIST 5)
		case 58:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox85->Text = msg; 
					cmc_form->textBox177->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox177->Text = msg;
					cmc_form->textBox85->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		// DUPES OF CDUX/Y/Z AND CDUT
		case 59:
		case 60:
		// DUPES OF ADOTx/y/z
		case 61:
		case 62:
		case 63:
		// DUPES OF AK-AK2 and RCSFLAGS
		case 64:
		case 65:
		// DUPES OF THEDADx/y/z and DELCDUx (LIST 1,2,3,4)
		// ERRORx/y/z and DUPE OF THETADx   (LIST 5)
		case 66:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox111->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox110->Text = msg;
					break;
			}
			break;
		case 67:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox109->Text = msg;
					// sprintf(msg,"%05o",cmc_w1); cmc_form->textBox110->Text = msg;
					break;
			}
			break;
		// RSBBQ and Q        (LIST 1,2,3,4)
		// DUPES OF THETADy/z (LIST 5)
		case 68:
			switch(cmc_lock_type){
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox86->Text = msg; break;
				case 2:
					break;
			}
			break;
		// CADRFLSH and FAILREG          (LIST 1,2,3,4)
		// DUPES OF CMDAPMOD and PREL    (LIST 5)
		case 69: 
			switch(cmc_lock_type){
				case 2:
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox90->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox89->Text = msg;
					break;
			}
			break;
		// DUPES OF QREL,RREL (LIST 5)
		case 70: 
			switch(cmc_lock_type){
				case 2:
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox88->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox99->Text = msg;
					break;
			}
			break;
		/* ****************** LIST 5 UPBUF DUPE STARTS ************** */
		case 71: 
			switch(cmc_lock_type){
				case 2:
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox98->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox97->Text = msg;
					break;
			}
			break;
		// CDUS and PIPAx/y/z
		case 72:
			switch(cmc_lock_type){
				case 2:
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox12->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox92->Text = msg;
					break;
			}
			break;
		case 73: 
			switch(cmc_lock_type){
				case 2:
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox91->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox93->Text = msg;
					break;
			}
			break;
		// O/I/M gyro torque angles       (LIST 1)
		// ELEV,CENTANG,DELTAR            (LIST 2,3)
		// 8NN,S22LOC,FLAGWORD 10-11,RLSx (LIST 4)
		case 74:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox96->Text = msg;
					cmc_form->textBox143->Text = "XXXXX-XXXXX";
					break;
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox143->Text = msg;
					cmc_form->textBox96->Text = "XXXXX-XXXXX";
					break;
				case 2:
					break;
			}
			break;
		case 75:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox94->Text = msg;
					cmc_form->textBox142->Text = "XXXXX-XXXXX";
					break;
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox142->Text = msg;
					cmc_form->textBox94->Text = "XXXXX-XXXXX";
					break;
				case 2:
					break;
			}
			break;
		case 76:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox95->Text = msg;
					cmc_form->textBox141->Text = "XXXXX-XXXXX";
					break;
				case 3:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox141->Text = msg;
					cmc_form->textBox95->Text = "XXXXX-XXXXX";
					break;
				case 2:
					break;
			}
			break;
		// FLAGWORD 10-11 (LIST 1,3)
		// DELVEET3x      (LIST 2)
		// RLSy           (LIST 4)
		case 77: 
			switch(cmc_lock_type){
				case 1:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox87->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox100->Text = msg;
					cmc_form->textBox146->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox146->Text = msg; break;
					break;
				case 2:
					break;
			}
			break;
		// TEVENT    (LIST 1,3)
		// DELVEET3y (LIST 2)
		// RLSz      (LIST 4)
		case 78:
			switch(cmc_lock_type){
				case 1:
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox102->Text = msg;
					cmc_form->textBox145->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox145->Text = msg;
					cmc_form->textBox102->Text = "XXXXX-XXXXX";
					break;
				case 2:
					break;
			}
			break;
		// LAUNCHAZ            (LIST 1)
		// DELVEET3z           (LIST 2)
		// DUPES OF PCMD,YCMD  (LIST 3)
		// SPARE               (LIST 4)
		case 79:
			switch(cmc_lock_type){
				case 1:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox101->Text = msg;
					cmc_form->textBox144->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox144->Text = msg;
					cmc_form->textBox101->Text = "XXXXX-XXXXX";
					break;
				case 4:
					cmc_form->textBox101->Text = "XXXXX-XXXXX";
					cmc_form->textBox144->Text = "XXXXX-XXXXX";
					break;
				case 2:
					break;
			}
			break;
		// OPTMODES and HOLDFLAG
		/* ****************** LIST 5 UPBUF DUPE ENDS **************** */
		case 80: 
			switch(cmc_lock_type){
				case 2:
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox103->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox106->Text = msg;
					break;
			}
			break;
		// LEMMASS and CSMMASS
		case 81: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox105->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox104->Text = msg;
			break;
		// DAPDATR1 and DAPDATR2
		case 82: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox107->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox108->Text = msg;
			break;
		// ERRORx/y/z and THETADx   (LIST 1,2,3,4)
		// DUPE OF ROLLTM and ROLLC (LIST 5)
		case 83: 
			switch(cmc_lock_type){
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox165->Text = msg;
					break;
				case 1:
				case 3:
				case 4:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox111->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox110->Text = msg;
					// cmc_form->textBox165->Text = "XXXXX-XXXXX"; // Gets blanked earlier.
					break;
			}
			break;
		// OPTMODES and HOLDFLAG
		case 84: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox109->Text = msg;
			//sprintf(msg,"%05o",cmc_w1); cmc_form->textBox108->Text = msg;
			break;
		// DESIRED R/P/Y RATES
		case 85:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox113->Text = msg; break;
		case 86:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox112->Text = msg; break;
		case 87:
			sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox114->Text = msg; break;
		// REDOCTR and THETAD
		case 88: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox115->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox118->Text = msg;
			break;
		case 89: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox117->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox116->Text = msg;
			break;
		// IMODES30 and IMODES33
		case 90: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox119->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox120->Text = msg;
			break;
		// CH11-12
		case 91: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox121->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox122->Text = msg;
			break;
		// CH13-14
		case 92: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox123->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox124->Text = msg;
			break;
		// CH30-31
		case 93: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox125->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox126->Text = msg;
			break;
		// CH32-33
		case 94: 
			sprintf(msg,"%05o",cmc_w0); cmc_form->textBox128->Text = msg;
			sprintf(msg,"%05o",cmc_w1); cmc_form->textBox127->Text = msg;
			break;
		// DUPES OF DSPTAB                                    (LIST 1)
		// RTHETA,LATSPL,LNGSPL,VPRED,GAMMAEI,FLAGWORD 10-11  (LIST 2)
		// VGTIGx/y/z and SPARES                              (LIST 3)
		// SPARES                                             (LIST 4)
		// RSBBQ,CADRFLSH,FAILREG,FLAGWORD 10-11,GAMMAEI,RTGO (LIST 5)
		case 95:
			switch(cmc_lock_type){
				case 1:
					cmc_form->textBox147->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox86->Text = msg;
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox147->Text = msg;
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox48->Text = msg;
					cmc_form->textBox147->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		case 96:
			switch(cmc_lock_type){
				case 1:
					cmc_form->textBox148->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox90->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox89->Text = msg;
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox148->Text = msg;
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox47->Text = msg;
					cmc_form->textBox148->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		case 97:
			switch(cmc_lock_type){
				case 1:
					cmc_form->textBox149->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox88->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox99->Text = msg;
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox149->Text = msg;
					break;
				case 4:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox46->Text = msg;
					cmc_form->textBox149->Text = "XXXXX-XXXXX";
					break;
			}
			break;
		case 98:
			switch(cmc_lock_type){
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox98->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox97->Text = msg;
					break;
				case 1:
				case 4:
					cmc_form->textBox150->Text = "XXXXX-XXXXX";
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox150->Text = msg;
					break;
			}
			break;
		case 99:
			switch(cmc_lock_type){
				case 1:
				case 4:
					cmc_form->textBox151->Text = "XXXXX-XXXXX";
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox87->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox100->Text = msg;
					break;
				case 3:
					sprintf(msg,"%05o-%05o",cmc_w0,cmc_w1); cmc_form->textBox151->Text = msg;
					break;
			}
			break;
		case 100:
			switch(cmc_lock_type){
				case 1:
				case 4:
					break;
				case 3:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox87->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox100->Text = msg;
					break;
				case 2:
					sprintf(msg,"%05o",cmc_w0); cmc_form->textBox151->Text = msg;
					sprintf(msg,"%05o",cmc_w1); cmc_form->textBox179->Text = msg;
					break;
			}
			break;
	}
}

// Translate bits into DSKY display character
char Form1::get_dsky_char(unsigned int bits){
	switch(bits){
		case 025:
			return('0');
		case 003:
			return('1');
		case 031:
			return('2');
		case 033:
			return('3');
		case 017:
			return('4');
		case 036:
			return('5');
		case 034:
			return('6');
		case 023:
			return('7');
		case 035:
			return('8');
		case 037:
			return('9');
		case 000:
			return('_');
		default:
			return('X');
	}
}

void Form1::parse_lbr(unsigned char data, int bytect){
	char msg[256];
	double value;
	switch(bytect){
		case 0: // SYNC 1
			if(data != 05){ end_lbr(); lock_type = 0; cmc_lock_type = 0; }
			break;
		case 1: // SYNC 2
			if(data != 0171){ end_lbr(); lock_type = 0; cmc_lock_type = 0; }
			break;
		case 2: // SYNC 3
			if(data != 0267){ end_lbr(); lock_type = 0; cmc_lock_type = 0; }
			break;
		case 3: // FRAME COUNT
			framect = data&077;
			break;
		case 4:
			switch(framect){
				case 1:
					if(eps_form != NULL){
						value = unscale_data(data,0,100);
						sprintf(msg,"%+05.2f A",value);
						eps_form->s11A109->Enabled = TRUE;
						eps_form->s11A109->Text = msg;						
					}
					break;
				case 4:
					if(eps_form != NULL){
						value = unscale_data(data,0,45);
						sprintf(msg,"%+04.2f V",value);
						eps_form->s11A91->Enabled = TRUE;
						eps_form->s11A91->Text = msg;						
					}
					break;
			}
			break;
		case 6:
			switch(framect){
				case 4:
					if(eps_form != NULL){
						value = unscale_data(data,0,45);
						sprintf(msg,"%+04.2f V",value);
						eps_form->s11A93->Enabled = TRUE;
						eps_form->s11A93->Text = msg;						
					}
					break;
			}
			break;
		case 8:
		case 28: // CMC DATA WORD
			cmc_w0 = data&0177;
			cmc_w0 <<= 8;
			break;
		case 9:
		case 29: // CMC DATA WORD
			cmc_w0 |= data&0377;
			break;
		case 10:
		case 30: // CMC DATA WORD
			cmc_w1 = data&0177;
			cmc_w1 <<= 8;
			break;
		case 11:
		case 31: // CMC DATA WORD
			cmc_w1 |= data&0377;
			break;
		case 12:
		case 32: // CMC DATA WORD
			parse_cmc();
			break;


	}
}

// Service Thread
void Form1::CommThread(){
	int bytesRecv = SOCKET_ERROR;
	unsigned char recvbuf[1024];
	char msg[256];
	int die=0;

	int frame_count = 0;
	int word_addr = 0;
	int byte_offset = 0;
	int bytect = 0;

	lock_type = 0;   cmc_lock_type = 0;
	frame_addr = 0;	 cmc_frame_addr = 0;
	framect = 0;     cmc_framect = 0;
	while(!die){
		bytesRecv = recv( m_socket, (char *)recvbuf, 1024, 0 );
		if(bytesRecv == SOCKET_ERROR){
			sprintf(msg,"Failed to read, Error %ld",WSAGetLastError());
			closesocket(m_socket);
			StatusBox->Text = msg;
			ConnectBtn->Text = "Connect";
			conn_status = 0;
			return;
		}else{		
			// sprintf(msg,"Got %d bytes",bytesRecv);
			StatusBox->Text = msg;
			byte_offset = 0;
			while(byte_offset < bytesRecv){
				switch(lock_type){
					case 0: // OUT SYNC 0
						StatusBox->Text = "LOCK/OUT FRAM/000 WORD/000";
						bytect = 0;
						if(recvbuf[byte_offset] == 05){ // Sync char 1 recieved
							lock_type = 1;
						}
						break;

					case 1: // OUT SYNC 1
						StatusBox->Text = "LOCK/OUT FRAM/000 WORD/001";
						if(recvbuf[byte_offset] == 0171){ // Sync char 2 recieved
							lock_type = 2;
						}else{
							lock_type = 0;
						}
						break;

					case 2: // OUT SYNC 2
						StatusBox->Text = "LOCK/OUT FRAM/000 WORD/002";
						if(recvbuf[byte_offset] == 0267){ // Sync char 3 recieved
							lock_type = 3;
							bytect = 3; // Start at byte 3
						}else{
							lock_type = 0;
						}
						break;

					case 3: // CHECK FOR LBR SYNC 1
						if(bytect == 40){
							if(recvbuf[byte_offset] == 05){ // Sync char 1 recieved
								lock_type = 4; // Check LBR SYNC 2
							}else{
								// Check for HBR instead
								sprintf(msg,"FAILED AT LB1 - GOT %o",recvbuf[byte_offset]);
								StatusBox->Text = msg;
								lock_type = 6;
								bytect++;
								break;
							}
						}else{
							if(bytect > 100){ bytect = 0; lock_type=0; } // Start over 
						}
						bytect++;
						sprintf(msg,"LOCK/OUT FRAM/000 WORD/%.3d LB1",bytect);
						StatusBox->Text = msg;
						break;

					case 4: // CHECK FOR LBR SYNC 2
						if(recvbuf[byte_offset] == 0171){ // Sync char 2 recieved
							lock_type = 5; // Check LBR SYNC 3
						}else{
							// Check for HBR instead
							lock_type = 6;
						}
						bytect++;
						sprintf(msg,"LOCK/OUT FRAM/000 WORD/%.3d LB2",bytect);
						StatusBox->Text = msg;
						break;

					case 5: // CHECK FOR LBR SYNC 3
						if(recvbuf[byte_offset] == 0267){ // Sync char 2 recieved
							lock_type = 10; // LBR SYNC
							bytect = 3;
							break;
						}else{
							// Check for HBR instead
							lock_type = 6;
						}
						bytect++;
						sprintf(msg,"LOCK/OUT FRAM/000 WORD/%.3d LB3",bytect);
						StatusBox->Text = msg;
						break;
						
					case 6: // CHECK FOR HBR SYNC 1
						if(bytect == 128){
							if(recvbuf[byte_offset] == 05){ // Sync char 1 recieved
								lock_type = 7; // Check HBR SYNC 2
							}else{
								// Fail
								lock_type = 0;
								break;
							}
						}else{
							if(bytect > 200){ bytect = 0; lock_type=0; } // Start over 
						}
						bytect++;
						sprintf(msg,"LOCK/OUT FRAM/000 WORD/%.3d HB1",bytect);
						StatusBox->Text = msg;
						break;

					case 7: // CHECK FOR HBR SYNC 2
						if(recvbuf[byte_offset] == 0171){ // Sync char 2 recieved
							lock_type = 8; // Check HBR SYNC 3
						}else{
							// Fail
							lock_type = 0;
							break;
						}
						bytect++;
						sprintf(msg,"LOCK/OUT FRAM/000 WORD/%.3d HB2",bytect);
						StatusBox->Text = msg;
						break;

					case 8: // CHECK FOR HBR SYNC 3
						if(recvbuf[byte_offset] == 0267){ // Sync char 3 recieved
							lock_type = 20; // HBR SYNC
							bytect = 3;
							break;
						}else{
							// Fail
							lock_type = 0;
							break;
						}
						bytect++;
						sprintf(msg,"LOCK/OUT FRAM/000 WORD/%.3d HB3",bytect);
						StatusBox->Text = msg;
						break;

					case 9: // DEBUG - STALL
						break;

					case 10: // LBR
						parse_lbr(recvbuf[byte_offset],bytect);
						bytect++;
						if(bytect > 39){
							bytect = 0;
							sprintf(msg,"LOCK/LBR FRAM/%.3d",framect,bytect);
							StatusBox->Text = msg;
							frame_addr++;
							if(frame_addr > 4){
								frame_addr = 0;
							}
						}
						break;

					case 20: // HBR
						parse_hbr(recvbuf[byte_offset],bytect);
						bytect++;
						if(bytect > 127){
							bytect = 0;
							sprintf(msg,"LOCK/HBR FRAM/%.3d",framect,bytect);
							StatusBox->Text = msg;
							frame_addr++;
							if(frame_addr > 4){
								frame_addr = 0;
							}
						}
						break;
				}
				byte_offset++;
			}
		}
	}
}

void Form1::ShowStructures(){
	if(str_form == NULL){
		str_form = new StructuresForm();
	}
	str_form->Show();
}

void Form1::ShowEPS(){
	if(eps_form == NULL){
		eps_form = new EPSForm();
	}
	eps_form->Show();
}

void Form1::ShowELS(){
	if(els_form == NULL){
		els_form = new ELSForm();
	}
	els_form->Show();
}

void Form1::ShowECS(){
	if(ecs_form == NULL){
		ecs_form = new ECSForm();
	}
	ecs_form->Show();
}

void Form1::ShowGNC(){
	if(gnc_form == NULL){
		gnc_form = new GNCForm();
	}
	gnc_form->Show();
}

void Form1::ShowSCS(){
	if(scs_form == NULL){
		scs_form = new SCSForm();
	}
	scs_form->Show();
}

void Form1::ShowSPS(){
	if(sps_form == NULL){
		sps_form = new SPSForm();
	}
	sps_form->Show();
}

void Form1::ShowCrew(){
	if(crw_form == NULL){
		crw_form = new CrewForm();
	}
	crw_form->Show();
}

void Form1::ShowTelecom(){
	if(tcm_form == NULL){
		tcm_form = new TelecomForm();
	}
	tcm_form->Show();
}

void Form1::ShowCMC(){
	if(cmc_form == NULL){
		cmc_form = new CMCForm();
	}
	cmc_form->Show();
}

void Form1::ShowUplink(){
	// The uplink form uses the CMC form's data.
	if(cmc_form == NULL){
		cmc_form = new CMCForm();
		cmc_form->Hide();
	}
	if(upl_form == NULL){
		upl_form = new UplinkForm();
		upl_form->m_socket = &m_socket;
	}
	upl_form->Show();
}

// Uplink Helpers
void Form1::cmc_uplink_word(char * data){
	upl_form->send_agc_key(data[0]);
	upl_form->send_agc_key(data[1]);
	upl_form->send_agc_key(data[2]);
	upl_form->send_agc_key(data[3]);
	upl_form->send_agc_key(data[4]);
	upl_form->send_agc_key('E');
}

// Process uplink from uplink form
// This is here because passing pointers to arrays in Managed C++ is a royal pain in the ass.

void Form1::cmc_uplink_process(){

	char tmp[256];
	unsigned int tmi=0;

	if(upl_form == NULL){ return; }
	switch(upl_form->cmc_uplink_state){
		case 0:  // IDLE
			return;

		case 1: // COMMAND START
			switch(upl_form->comboBox1->SelectedIndex){
				case 1:  // V70 LIFTOFF TIME INCREMENT
					upl_form->cmc_uplink_state = 10;
					break;

				case 2: // V71 CONTIGUOUS ERASABLE
				case 3: // V71 STATE VECTOR
				case 4: // V71 DESIRED REFSMMAT
				case 5: // V71 CMC REFSMMAT
				case 6: // V71 EXTERNAL DV
				case 7: // V71 ENTRY DATA + EXTERNAL DV
				case 8: // V71 ENTRY DATA
				case 9: // V71 LAMBERT TARGET
				case 10: // V71 LUNAR LANDING SITE
					upl_form->cmc_uplink_state = 20;
					break;

				case 11: // V72 NONCONTIGUOUS ERASABLE UPDATE
					upl_form->cmc_uplink_state = 70;
					break;

				case 12: // V73 OCTAL CLOCK INCREMENT
					upl_form->cmc_uplink_state = 72;
					break;
			}
			break;

		case 10: // VERB 70 START
			upl_form->textBox26->Text = "SENDING V70";
			upl_form->send_agc_key('V');
			upl_form->send_agc_key('7');
			upl_form->send_agc_key('0');
			upl_form->send_agc_key('E');
			upl_form->cmc_uplink_state++;
			break;

		case 11: // V70 - VERIFY P27
			upl_form->textBox26->Text = "AWAITING P27";
			if(cmc_upcount != 1){ break; }
			if(cmc_lock_type != 2){ break; }
			if(cmc_upverb != 0){ break; }
			if(cmc_compnumb != 2){ break; }
			upl_form->cmc_uplink_state++;
			break;

		case 12: // TRANSMIT WORD 0
			upl_form->textBox26->Text = "SENDING WORD 0";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox6->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);
			sprintf(tmp,"%05o",tmi);
			upl_form->textBox6->Text = tmp;
			cmc_uplink_word(tmp);
			upl_form->cmc_uplink_state++;
			break;

		case 13: // VERIFY WORD 0
			sprintf(tmp,"VERIFYING WORD 0 VS %05o",cmc_upbuff[0]);
			upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox6->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);
			if(tmi != cmc_upbuff[0]){ break; }
			upl_form->cmc_uplink_state++;
			break;

		case 14: // TRANSMIT WORD 1
			upl_form->textBox26->Text = "SENDING WORD 1";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox7->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);
			sprintf(tmp,"%05o",tmi);
			upl_form->textBox7->Text = tmp;
			cmc_uplink_word(tmp);
			upl_form->cmc_uplink_state++;
			break;

		case 15: // VERIFY WORD 1
			sprintf(tmp,"VERIFYING WORD 1 VS %05o",cmc_upbuff[1]);
			upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox7->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);
			if(tmi != cmc_upbuff[0]){ break; }
			upl_form->cmc_uplink_state++;
			break;

		case 16: // COMMIT V70
			upl_form->textBox26->Text = "COMPLETED";
			upl_form->send_agc_key('V');
			upl_form->send_agc_key('3');
			upl_form->send_agc_key('3');
			upl_form->send_agc_key('E');
			// Re-enable controls
			upl_form->button64->Enabled = true;
			upl_form->comboBox1->Enabled = true;
			upl_form->textBox26->Enabled = false;
			// Go to idle
			upl_form->cmc_uplink_state = 0;
			break;			

		case 20: // VERB 71 START
			upl_form->textBox26->Text = "SENDING V71";
			upl_form->send_agc_key('V');
			upl_form->send_agc_key('7');
			upl_form->send_agc_key('1');
			upl_form->send_agc_key('E');
			upl_form->cmc_uplink_state++;
			break;

		case 21: // V71 - VERIFY P27
			upl_form->textBox26->Text = "AWAITING P27";
			if(cmc_upcount != 1){ break; }
			if(cmc_lock_type != 2){ break; }
			if(cmc_upverb != 1){ break; }
			upl_form->cmc_uplink_state++;
			break;

		case 22: // TRANSMIT INDEX
			upl_form->textBox26->Text = "SENDING INDEX";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox5->Text));
			tmi = (unsigned int)strtol(tmp,NULL,10);
			sprintf(tmp,"%02o",tmi);
			cmc_upindex = tmi; // Save index
			// Don't write back the index, leave it decimal
			upl_form->send_agc_key(tmp[0]);
			upl_form->send_agc_key(tmp[1]);
			upl_form->send_agc_key('E');
			upl_form->cmc_uplink_state++;
			break;
		case 23: // VERIFY INDEX
			sprintf(tmp,"VERIFYING INDEX %02o VS %02o",cmc_upindex,cmc_upbuff[0]);
			upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox5->Text));
			tmi = (unsigned int)strtol(tmp,NULL,10);
			if(tmi != cmc_upbuff[0]){ break; }
			upl_form->cmc_uplink_state++;
			break;

		case 24: // TRANSMIT WORD 0
			upl_form->textBox26->Text = "SENDING WORD 0";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox6->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox6->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 25: // VERIFY WORD 0
			sprintf(tmp,"VERIFYING WORD 0 VS %05o",cmc_upbuff[1]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox6->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); if(tmi != cmc_upbuff[1]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 26: // TRANSMIT WORD 1
			upl_form->textBox26->Text = "SENDING WORD 1";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox7->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	sprintf(tmp,"%05o",tmi); upl_form->textBox7->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++; break;
		case 27: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 1 VS %05o",cmc_upbuff[2]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox7->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); if(tmi != cmc_upbuff[2]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 28: // TRANSMIT WORD 2
			if(cmc_upindex == 3){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 2";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox9->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox9->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 29: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 2 VS %05o",cmc_upbuff[3]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox9->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); if(tmi != cmc_upbuff[3]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 30: // TRANSMIT WORD 3
			if(cmc_upindex == 4){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 3";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox8->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox8->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 31: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 3 VS %05o",cmc_upbuff[4]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox8->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[4]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 32: // TRANSMIT WORD 4
			if(cmc_upindex == 5){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 4";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox11->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox11->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 33: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 4 VS %05o",cmc_upbuff[5]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox11->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[5]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 34: // TRANSMIT WORD 5
			if(cmc_upindex == 6){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 5";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox10->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox10->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 35: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 5 VS %05o",cmc_upbuff[6]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox10->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[6]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 36: // TRANSMIT WORD 6
			if(cmc_upindex == 7){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 6";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox13->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox13->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 37: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 6 VS %05o",cmc_upbuff[7]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox13->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[7]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 38: // TRANSMIT WORD 7
			if(cmc_upindex == 8){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 7";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox12->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox12->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 39: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 7 VS %05o",cmc_upbuff[8]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox12->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[8]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 40: // TRANSMIT WORD 8
			if(cmc_upindex == 9){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 8";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox15->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox15->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 41: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 8 VS %05o",cmc_upbuff[9]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox15->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[9]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 42: // TRANSMIT WORD 9
			if(cmc_upindex == 10){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 9";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox14->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox14->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 43: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 9 VS %05o",cmc_upbuff[10]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox14->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[10]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 44: // TRANSMIT WORD 10
			if(cmc_upindex == 11){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 10";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox25->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox25->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 45: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 10 VS %05o",cmc_upbuff[11]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox25->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[11]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 46: // TRANSMIT WORD 11
			if(cmc_upindex == 12){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 11";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox24->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox24->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 47: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 11 VS %05o",cmc_upbuff[12]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox24->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[12]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 48: // TRANSMIT WORD 12
			if(cmc_upindex == 13){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 12";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox23->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox23->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 49: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 12 VS %05o",cmc_upbuff[13]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox23->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[13]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 50: // TRANSMIT WORD 13
			if(cmc_upindex == 14){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 13";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox22->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox22->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 51: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 13 VS %05o",cmc_upbuff[14]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox22->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[14]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 52: // TRANSMIT WORD 14
			if(cmc_upindex == 15){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 14";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox21->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox21->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 53: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 14 VS %05o",cmc_upbuff[15]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox21->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[15]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 54: // TRANSMIT WORD 15
			if(cmc_upindex == 16){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 15";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox20->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox20->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 55: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 15 VS %05o",cmc_upbuff[16]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox20->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[16]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 56: // TRANSMIT WORD 16
			if(cmc_upindex == 17){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 16";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox19->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox19->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 57: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 16 VS %05o",cmc_upbuff[17]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox19->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[17]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 58: // TRANSMIT WORD 17
			if(cmc_upindex == 18){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 17";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox18->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox18->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 59: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 17 VS %05o",cmc_upbuff[18]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox18->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[18]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 60: // TRANSMIT WORD 18
			if(cmc_upindex == 19){ upl_form->cmc_uplink_state = 62; break; } // DONE
			upl_form->textBox26->Text = "SENDING WORD 18";
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox17->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8); sprintf(tmp,"%05o",tmi); upl_form->textBox17->Text = tmp;
			cmc_uplink_word(tmp); upl_form->cmc_uplink_state++;	break;
		case 61: // VERIFY WORD
			sprintf(tmp,"VERIFYING WORD 18 VS %05o",cmc_upbuff[19]); upl_form->textBox26->Text = tmp;
			sprintf(tmp,"%s",System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(upl_form->textBox17->Text));
			tmi = (unsigned int)strtol(tmp,NULL,8);	if(tmi != cmc_upbuff[19]){ break; }
			upl_form->cmc_uplink_state++; break;

		case 62: // COMMIT 
			upl_form->textBox26->Text = "COMPLETED";
			upl_form->send_agc_key('V');
			upl_form->send_agc_key('3');
			upl_form->send_agc_key('3');
			upl_form->send_agc_key('E');
			// Re-enable controls
			upl_form->button64->Enabled = true;
			upl_form->comboBox1->Enabled = true;
			upl_form->textBox26->Enabled = false;
			// Go to idle
			upl_form->cmc_uplink_state = 0;
			break;			

		case 70: // VERB 72 START
			upl_form->textBox26->Text = "SENDING V72";
			upl_form->send_agc_key('V');
			upl_form->send_agc_key('7');
			upl_form->send_agc_key('2');
			upl_form->send_agc_key('E');
			upl_form->cmc_uplink_state++;
			break;

		case 71: // V72 - VERIFY P27
			upl_form->textBox26->Text = "AWAITING P27";
			if(cmc_upcount != 1){ break; }
			if(cmc_lock_type != 2){ break; }
			if(cmc_upverb != 2){ break; }
			upl_form->cmc_uplink_state = 22; // CONTINUE HERE
			break;

		case 72: // VERB 73 START
			upl_form->textBox26->Text = "SENDING V73";
			upl_form->send_agc_key('V');
			upl_form->send_agc_key('7');
			upl_form->send_agc_key('3');
			upl_form->send_agc_key('E');
			upl_form->cmc_uplink_state++;
			break;

		case 73: // V73 - VERIFY P27
			upl_form->textBox26->Text = "AWAITING P27";
			if(cmc_upcount != 1){ break; }
			if(cmc_lock_type != 2){ break; }
			if(cmc_upverb != 3){ break; }
			if(cmc_compnumb != 2){ break; }
			upl_form->cmc_uplink_state = 12; // CONTINUE HERE
			break;

	}
}