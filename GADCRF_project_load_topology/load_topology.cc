/*
 * load_topology.cc
 *
 *      Author: Roberta Fumarola, David Costa, Gaetano Alboreto
 * Description: Parsing from XML file to class Topology C++
 */

#include "header_project.h"

void installLSP(Topology *net,int nodes);
void installLSPdemo(Topology *net,int nodes);
void configureNet(Topology *net,int nodes);
void configureNetdemo(Topology *net,int nodes);
char *itoa(int i);

int id=0;
int simul;


int main(int argc, char *argv[]) {

	struct xmlRoot2 *xmlTopology;
	int nodes = 0;
	char *command;

	xmlTopology = (struct xmlRoot2*) malloc(sizeof(struct xmlRoot2));

	int mode;
	Topology *net;

	while(mode!=0 && mode!= 1 && mode!=2){
		printf("Select mode(0=GNS3, 1=real, 2=demo)\n> ");
		scanf("%i",&mode);

		switch(mode){
		case 0:

			//Import from XML file topology_xml
			simul=0;
			ImportTopology(xmlTopology);
			nodes = xmlTopology->nodes;
			net = new Topology(nodes);
			net->LoadTopology(xmlTopology);

			//Enable tap interface and test it
			printf("Enable tap interface and test it\n");

			system("./script/config_tap.sh");
			printf("test\n");
			for (int i=0;i<nodes;i++){
					command = (char*)calloc(CHAR_COMMAND,sizeof(char));
					strcpy(command,"ping ");
					strcat(command,net->LoopArray()[i].loopAddr);
					strcat(command," -c 3");
					system(command);
			}
			printf("test ended\n");
			break;
		case 1:
			printf("Real mode\n");
			//Import from XML file topology_xml_simul
			simul=0;
			ImportTopology(xmlTopology);
			nodes = xmlTopology->nodes;
			net = new Topology(nodes);
			net->LoadTopology(xmlTopology);
			printf("test\n");
			for (int i=0;i<nodes;i++){
					command = (char*)calloc(CHAR_COMMAND,sizeof(char));
					strcpy(command,"ping ");
					strcat(command,net->LoopArray()[i].loopAddr);
					strcat(command," -c 3");
					system(command);
			}
			printf("test ended\n");
			break;
		case 2:
			//Import from XML file topology_xml_simul
			simul=1;
			ImportTopology(xmlTopology);
			nodes = xmlTopology->nodes;
			net = new Topology(nodes);
			net->LoadTopology(xmlTopology);
			printf("Demo mode\n");
			break;
		default:
			printf("Command not found.\n");
			break;
		}
	}

	int choise;
	while(1){
		printf("********* MENU *********\n");
		printf("1: Print adjacency matrix\n");
		printf("2: Configure net\n");
		printf("3: Install LSP\n");
		printf("4: Exit\n");
		printf("> ");
		scanf("%i",&choise);
		switch(choise){
		case 1:
			net->PrintAdjMatrix();
			break;
		case 2:
			if(mode==2)
				configureNetdemo(net,nodes);
			else
				configureNet(net,nodes);
			break;
		case 3:
			if(mode==0 || mode==1)
				installLSP(net,nodes);
			else if(mode==2)
				installLSPdemo(net,nodes);
			break;
		case 4:
			return 0;
		default:
			printf("Command not found\n");
			break;
		}
	}

	return 0;
}

void installLSP(Topology *net,int nodes){

	int size;
	int capacity=-1;
	int src=-1;
	int dst=-1;
	char *command;

	while(src<0 || src>=nodes){
		printf("Source node:\n> ");
		scanf("%i",&src);
		if(src<0 || src>=nodes)
			printf("Source node not valid\n");
	}
	while(dst<0 || dst>=nodes){
		printf("Destination node:\n> ");
		scanf("%i",&dst);
		if(dst<0 || dst>=nodes)
			printf("Destination node not valid\n");
	}
	while(capacity<0){
		printf("Capacity:\n> ");
		scanf("%i",&capacity);
		if(capacity<0)
			printf("Negative capacity not valid\n");
	}
	int* path = find_path(net->Matrix(),nodes,src,dst,capacity,&size);
	if(path==NULL){
		printf("It's not possible to install an LSP\n");
		return;
	}
	net->UpdateTopology(path,size,capacity);
	command = (char*)calloc(CHAR_COMMAND,sizeof(char));
	strcpy(command,"expect ./script/lsp.sh ");
	strcat(command,net->LoopArray()[path[0]].loopAddr);//insert IP
	strcat(command," ");
	strcat(command,net->LoopArray()[path[size-1]].loopAddr);//insert DEST
	strcat(command," ");
	char *buffer;
	buffer = itoa(capacity);
	strcat(command,buffer);//Insert CAPACITY
	strcat(command," ");
	buffer = itoa(id++);
	strcat(command,buffer);//Insert ID
	strcat(command," ");
	for(int i=0;i<size-1;i++){
		strcat(command,net->Matrix()[path[i]][path[i+1]].dstAddr);//insert PATH
		strcat(command," ");
	}
	system(command);
}

void installLSPdemo(Topology *net,int nodes){
	int size;
	int capacity=-1;
	int src=-1;
	int dst=-1;

	while(src<0 || src>=nodes){
		printf("Source node:\n> ");
		scanf("%i",&src);
		if(src<0 || src>=nodes)
			printf("Source node not valid\n");
	}
	while(dst<0 || dst>=nodes){
		printf("Destination node:\n> ");
		scanf("%i",&dst);
		if(dst<0 || dst>=nodes)
			printf("Destination node not valid\n");
	}
	while(capacity<0){
		printf("Capacity:\n> ");
		scanf("%i",&capacity);
		if(capacity<0)
			printf("Negative capacity not valid\n");
	}
	int* path = find_path(net->Matrix(),nodes,src,dst,capacity,&size);
	if(path==NULL){
		printf("It's not possible to install an LSP\n");
		return;
	}
	int* path_unc = find_path_unconstrained(net->Matrix(),nodes,src,dst,&size);
	if(path_unc==NULL){
		printf("It's not possible to install an LSP\n");
		return;
	}
	char *buffer1,*buffer2;
	buffer1 = itoa(capacity);
	buffer2 = itoa(id++);
	net->UpdateTopology(path,size,capacity);
	showConfigureLSP(src,net->LoopArray()[path[0]].loopAddr,net->LoopArray()[path[size-1]].loopAddr,
			buffer1,buffer2,size-1,net->Matrix());

}

void configureNet(Topology *net,int nodes){
	char *command[nodes];
	int i,j;
	for (i=0;i<nodes;i++){
		command[i] = (char*)calloc(CHAR_COMMAND,sizeof(char));
		strcpy(command[i],"expect ./script/cef.sh ");
		strcat(command[i],net->LoopArray()[i].loopAddr);

		for(j=0;j<nodes;j++){

			if(net->Matrix()[i][j].capacity!=-1){
				strcat(command[i]," ");
				strcat(command[i],net->Matrix()[i][j].srcInterface);
			}
		}
		system(command[i]);
	}
}

void configureNetdemo(Topology *net,int nodes){
	showConfigureNet(net->LoopArray(),net->Matrix(),nodes);
}

//Conversion from int to string
char *itoa(int i)
{
	/* Room for INT_DIGITS digits, - and '\0' */
	static char buf[INT_DIGITS + 2];
	char *p = buf + INT_DIGITS + 1;	/* points to terminating '\0' */
	if (i >= 0) {
		do {
			*--p = '0' + (i % 10);
			i /= 10;
		} while (i != 0);
		return p;
	}
	else {			/* i < 0 */
		do {
			*--p = '0' - (i % 10);
			i /= 10;
		} while (i != 0);
		*--p = '-';
	}
	return p;
}
