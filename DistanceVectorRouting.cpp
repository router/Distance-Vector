/*
 * DistanceVectorRouting.cpp
 *
 *  Created on: Nov 1, 2013
 *      Author: subhranil
 */

#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<arpa/inet.h>
#include<cstdlib>
#include<netinet/in.h>
#include <ifaddrs.h>
#include<cstring>
#include<unistd.h>
#include<fcntl.h>
#include<pthread.h>
#include<errno.h>
#include <netdb.h>
#include <sys/time.h>
#include<signal.h>
#include<inttypes.h>
#define NODECOUNTMAX 100
#define MAXPACKETSIZE 10000
#define PROMPTVAR "DistanceVectorRouting>>"
#define MAXRETRY 3

int gTotalNumberOfNodes=0;
int gNumberOfNeighbours=0;
char gTopologyFileName[1024]="";
int gUpdateInterval=0;
char gLocalIP[50]="";
int16_t gLocalPort=0;
int32_t gLocalServerId=0;
struct timeval timeout;
int gUpdateCount=0;
typedef struct Node
{
	int16_t id;
	int32_t IP;
	int16_t port;
	int16_t nextHopId;
	int16_t cost;
	bool isActiveNeighbour;
	int updateCount;
} Node;

//typedef struct TimeNode
//{
//	struct timeval lastTime;
//	int interval;
//} TimeNode;

typedef struct sockaddr_in SocketAddressInfo;
//typedef struct DistanceVectorNode
//{
//	Node dest;
//
//} DistanceVectorNode;

//Node nodeList[NODECOUNTMAX];// also the routing table
Node costList[NODECOUNTMAX]={{-1,0,-1,-1,-1}}; // weight of the connecting edge. contains entries only for the neighbours of this host
Node distanceVector[NODECOUNTMAX];//[NODECOUNTMAX];// this 2d matrix and grow to the N*N size in the worst case.
int16_t costMatrix[NODECOUNTMAX][NODECOUNTMAX]={(int16_t)-1};
// For the first two lists index 0 must be self and hence the costList[0]=0;
// cost list (id,cost) for each f the neighbours.
// Distance vector is a map betwen (id,minCost) with size  gTotalNumberOfNodes( must be allowed room to expand)
// distance vect


void init()
{
	for(int i=0;i<NODECOUNTMAX;i++)
	{
		costList[i].IP=-1;
		costList[i].port=-1;
		costList[i].id=-1;
		costList[i].cost=-1;
		costList[i].nextHopId=-1;
		costList[i].updateCount=MAXRETRY;
		costList[i].isActiveNeighbour=false;

		for(int j=0;j<NODECOUNTMAX;j++)
		{
			costMatrix[i][j]=-1;
		}

	}
	memcpy(distanceVector,costList,sizeof(Node)*NODECOUNTMAX);
}

// This function returns the IP of the host
char* getLocalIP()
{
	SocketAddressInfo *socketAddress; // this structure basically stores info about the socket address
	socketAddress=(SocketAddressInfo*)malloc(sizeof(SocketAddressInfo));
	socketAddress->sin_family=AF_INET;
	socketAddress->sin_port=htons(53);
	inet_pton(socketAddress->sin_family,"8.8.8.8",&(socketAddress->sin_addr));

	//create a socket for connecting to server
	int socketId=socket(AF_INET,SOCK_DGRAM,0);
	if(socketId==-1)
	{
		printf("Error in creating socket");
		return "";
	}
	int retConnect=connect(socketId,(struct sockaddr*)socketAddress,sizeof(SocketAddressInfo));
	if(retConnect<0)
	{
		printf("connection with peer failed with error : %d",retConnect);
		return "";
	}

	SocketAddressInfo localAddressInfo;
	socklen_t len=sizeof(localAddressInfo);
	getsockname(socketId,(struct sockaddr*)&localAddressInfo,&len);
	char buffer[32];
	inet_ntop(AF_INET,&localAddressInfo.sin_addr,buffer,sizeof(buffer));
	return buffer;
}


int indexOfNodeWithId(int32_t id)
{
	for(int i=0;i<gTotalNumberOfNodes;i++)
	{
		if(costList[i].id==id)
			return i;
	}
	return -1;
}

int indexOfNodeWithIP(int32_t ip,int16_t port)
{
	for(int i=0;i<gTotalNumberOfNodes;i++)
	{
		if(costList[i].IP==ip&&costList[i].port==port)
			return i;
	}
	return -1;
}

//The function below creates the packet data from the distance vector of the host

char *createMessageFormatFromVector(int *numBytes)
{
		char *mssg=(char*)malloc(12*gTotalNumberOfNodes+8);
		memcpy(mssg+*numBytes,(int16_t*)&gTotalNumberOfNodes,2);
		*numBytes+=2;
		//memcpy(mssg+numberOfBytesWritten,(int16_t*))
		memcpy(mssg+*numBytes,&costList[0].port,2);
		*numBytes+=2;
//		int32_t tempIp=0;
//		inet_pton(AF_INET,,&tempIp);
		memcpy(mssg+*numBytes,&costList[0].IP,4);
		*numBytes+=4;
		for(int i=0;i<gTotalNumberOfNodes;i++)
		{
			//int32_t tempIp=0;
			//inet_pton(AF_INET,,&tempIp);
			memcpy(mssg+*numBytes,&distanceVector[i].IP,4);
			*numBytes+=4;
			memcpy(mssg+*numBytes,&distanceVector[i].port,2);
			*numBytes+=4;
			memcpy(mssg+*numBytes,&distanceVector[i].id,2);
			*numBytes+=2;
//			memcpy(mssg+*numBytes,&distanceVector[i].nextHopId,2);
//			*numBytes+=2;
			memcpy(mssg+*numBytes,&distanceVector[i].cost,2);
			*numBytes+=2;
		}
		return mssg;
}

//After receiving a message, the following function is called to parse the message and setup the cost matrix accordingly
Node* convertMessageIntoDistanceVector(char* message,int32_t *baseId)
{
	int16_t tempVar;
	int32_t temp;


	int numberOfBytesRead=0;
	memcpy(&tempVar,message+numberOfBytesRead,2);
	numberOfBytesRead+=2;
	Node* tempVector=(Node*)malloc(sizeof(Node)*tempVar); // creat the temp array
	//gTotalNumberOfNodes=(int)tempVar; //change .. check this line
	memcpy(&tempVar,message+numberOfBytesRead,2);
	numberOfBytesRead+=2;
	memcpy(&temp,message+numberOfBytesRead,4);
	numberOfBytesRead+=4;
//	char IP[50];
//	inet_ntop(AF_INET,&temp,IP,50);

	int baseindex=indexOfNodeWithIP(temp,tempVar);
	if(!costList[baseindex].isActiveNeighbour)
		return NULL;

	/*//change .. what happens if new
	neighbour(add to costlist)
	has to be a new neighbour --> add to cost List (non neighbours dont send Dv's)
	can be a newnon neighbour 0--> add directly to distance vector[0];
	*/
	//bool newNeighbour,newNode;
	if(baseindex<0) // should never happen .this means that the node was  not there in the topology file
	{
		//printf("%hd-->%d\n",tempVar,temp);
		printf("Invalid message received.\n");
		fflush(stdout);
		return NULL;
	}
	else
	{

		*baseId=costList[baseindex].id;


		for(int j=0;j<gTotalNumberOfNodes;j++)
		{
			memcpy(&temp,message+numberOfBytesRead,4);
			numberOfBytesRead+=4;
			memcpy(&tempVar,message+numberOfBytesRead,2);
			numberOfBytesRead+=4; // to handle the null
			int index=indexOfNodeWithIP(temp,tempVar);
			if(index<0) // this means that the node does not exist in the system .. should never happen.
			{
				//printf("%hd-->%d\n",tempVar,temp);
				printf("Invalid message received.\n");
				fflush(stdout);
				return NULL;
			}
			else
			{
				tempVector[index].IP=temp;
				tempVector[index].port=tempVar;
				memcpy(&tempVector[index].id,message+numberOfBytesRead,2);
				numberOfBytesRead+=2;
//				memcpy(&tempVector[index].nextHopId,message+numberOfBytesRead,2);
//				numberOfBytesRead+=2;
				memcpy(&tempVector[index].cost,message+numberOfBytesRead,2);
				numberOfBytesRead+=2;
				costList[baseindex].updateCount=MAXRETRY;
				costMatrix[baseindex][index]=tempVector[index].cost;

//				if(costList[baseindex].updateCount==0)
//					costList[baseindex].updateCount=MAXRETRY;
//				else
//					costList[baseindex].updateCount++;
//				// check if this is a new neighbour


				//The following code is not needed since the update command will be executed on both ends of the link

//				if(index==0&&costList[0].id==tempVector[index].nextHopId)
//				{
//					if(costList[baseindex].cost==-1)
//					{
//						printf("New neighbour added. id:%hd",costList[baseindex].id);
//						gNumberOfNeighbours++;
//					}
//					else if(costList[baseindex].cost!=tempVector[index].cost)
//						printf("Edge weight between %hd updated",costList[baseindex].id);
//
//					costList[baseindex].cost=tempVector[index].cost;
//					//printf("edge weight updated\n");
//					//gNumberOfNeighbours++;
//				}
			}

		}
		gUpdateCount++;
		printf("RECEIVED A MESSAGE FROM SERVER %hd\n",*baseId);
		fflush(stdout);
	}


	return tempVector;

}

// Flushes the distance vector of the host.
void resetDistanceVector()
{
	for (int i=1;i<gTotalNumberOfNodes;i++)
	{
		distanceVector[i].cost=-1;
		costMatrix[0][i]=-1;
	}
}


//Implements the Bellman Ford Algo to update the distance vector of the host. The isinit parameter toggles initialization and update cases.

void updateSelfDistanceVectorWithVector(bool isInit)
{
	//int index=indexOfNodeWithId(id);
	//int32_t baseCost=costList[index].cost; //can never be < 0
	//costMatrix[0][0]=0;
	for(int i=1; i<gTotalNumberOfNodes;i++) // we dont need update to self route
	{
		//bellman ford implementation

		if(isInit) // initialization case
		{
			distanceVector[i].cost=costList[i].cost;
			distanceVector[i].nextHopId=costList[i].id;
			costMatrix[0][i]=costList[i].cost;
			//costMatrix[i][i]=0;
			continue;
		}
		//int16_t minCost=costMatrix[0][i], minHop=distanceVector[i].nextHopId;
		int16_t minCost=-1,minHop=-1;
		// loop over all neighbours
		for(int j=1;j<gTotalNumberOfNodes;j++)
		{
			int16_t baseCost=costList[j].cost; //can never be < 0
			if(baseCost<0||!costList[j].isActiveNeighbour)// not a neighnour
				continue;

			//int16_t value=baseCost+costMatrix[j][i].cost;
			//printf("i:%d-->j%d-->minCost:%hd---->costMatrix[j][i]:%hd-->baseCost:%hd\n",i,j,minCost,costMatrix[j][i],baseCost);
			if(minCost==(int16_t)-1&&costMatrix[j][i]==(int16_t)-1) // you cant help me :( // neither your nor the peer can reach
			{
				if(i==j) // this new condition is being added since we now have something to reset
				{
					minCost=baseCost;
					minHop=costList[i].id;
				}
			}
			else if(minCost==(int16_t)-1) // you cannot reach but your peer can reach
			{
				minCost=baseCost+costMatrix[j][i];
				minHop=costList[j].id;
			}
			else if(costMatrix[j][i]!=(int16_t)-1)
			{
				int16_t value=baseCost+costMatrix[j][i];
				if(value<minCost)
				{
					minCost=value;
					minHop=costList[j].id;
				}
			}

		}// finished looping over neighbours

		if(minCost>0)
		{
			distanceVector[i].cost=minCost;
			distanceVector[i].nextHopId=minHop;
			costMatrix[0][i]=minCost;
		}

		/*int32_t value;=(costList[i].cost==(int16_t)-1||v[i].cost==(int16_t)-1)?-1:(costList[i].cost+v[i].cost) ;
		if(distanceVector[i].cost==-1)
			distanceVector[i].cost=value;
		else if(value<=distanceVector[i].cost)
		{
			distanceVector[i].cost=value;
			distanceVector[i].nextHopId=id;
		}*/
	}
}

// Sends out the distance vector to all active neighbours. Also checks if any of the neighbours have died.(3 misses case)
void broadcastDistanceVectorToNeighbours(int isAuto)
{
	int numberOfBytesWritten=0;
	char* message=createMessageFormatFromVector(&numberOfBytesWritten);
	// structure created successfully for sending.
	// send to all the neighbours
	for(int i=1;i<gTotalNumberOfNodes;i++)
	{
		if(costList[i].isActiveNeighbour)
		{
			// check for corresponding timer value
//			struct timeval currTime;
//			gettimeofday(&currTime,NULL);
			if(isAuto)
				costList[i].updateCount--;
			if(costList[i].updateCount==0)
			//if(timerList[i].lastTime.tv_sec!=0 && currTime.tv_sec-timerList[i].lastTime.tv_sec>=(3*gUpdateInterval))
			{
				costList[i].cost=-1;
//				distanceVector[i].cost=-1;
//				costMatrix[0][i]=-1;
				resetDistanceVector();
				updateSelfDistanceVectorWithVector(false);
				gNumberOfNeighbours--;
				printf("Neighbour with id:%hd has gone down.\n",costList[i].id);
				costList[i].updateCount=-1;
				continue;
			}
			//int sockId=createUDPSocket(costList[i].IP,costList[i].port);

			SocketAddressInfo *socketAddress; // this structure basically stores info about the socket address
			socketAddress=(SocketAddressInfo*)malloc(sizeof(SocketAddressInfo));
			socketAddress->sin_family=AF_INET;
			socketAddress->sin_port=htons(costList[i].port);
			socketAddress->sin_addr.s_addr=costList[i].IP;
			//inet_pton(socketAddress->sin_family,ip,&(socketAddress->sin_addr));

			//create a socket for connecting to server
			int socketId=socket(AF_INET,SOCK_DGRAM,0);
			int slen=sizeof(SocketAddressInfo);
			int retV=sendto(socketId,message,numberOfBytesWritten,0,(struct sockaddr*)socketAddress,(socklen_t)slen);
			//printf("err:%d-->ret:%d",errno,retV);
			fflush(stdout);
			close(socketId);

		}
	}
	free(message);
}

int compare(const void* a, const void* b)
{
	return (int)(((Node*)a)->id-((Node*)b)->id);
}

// prints the routing table in the specified format.
void displayRoutingTable()
{
	//printf("gTotalNumberOfNodes:%hd",gTotalNumberOfNodes);
	Node* tempDV=(Node*)malloc(sizeof(Node)*gTotalNumberOfNodes);
	memcpy(tempDV,distanceVector,sizeof(Node)*gTotalNumberOfNodes);
	qsort(tempDV,gTotalNumberOfNodes,sizeof(Node),&compare);
	for(int i=0;i<gTotalNumberOfNodes;i++)
	{
		char cost[8];
		if(tempDV[i].cost==-1)
			strcpy(cost,"inf");
		else
			sprintf(cost,"%hd",tempDV[i].cost);
		printf("%hd\t%hd\t%s\n",tempDV[i].id,tempDV[i].nextHopId,cost);
	}
	fflush(stdout);
}


void updateTimer(int id)
{

}

// Assumption : There will be less than 10 nodes in the setup.
int parseStringAndGetLocaLServerId(char *str)
{
	int i=0;
	int index=strlen(str)-2;
	while(index>0)
	{
		if(str[index]=='\n')
			break;
		index--;
	}
	//char *ptr=strrchr(str,'\n');
	//ptr++;
	return (str[index+1]-'0');

}

// Read the topology file and setup the costList[] and distanceVector[] arrays.
void initialiseLists()
{
	try
	{
	int fd=open(gTopologyFileName,O_RDONLY);
	//printf("%d",errno);
	//cgange remove hardbound by calc filesize by stat
	char buffer[5000];
	read(fd,buffer,5000);
	int baseId=parseStringAndGetLocaLServerId(buffer);
	char line[200],*saveptr1,*saveptr2;
	//strcpy(line,strtok_r(buffer,"\n",&saveptr1));
	strcpy(line,strtok(buffer,"\n"));

	gTotalNumberOfNodes=atoi(line);
	//strcpy(line,strtok_r(NULL,"\n",&saveptr1));
	strcpy(line,strtok(NULL,"\n"));
	gNumberOfNeighbours=atoi(line);
	for(int i=1,linecount=0;linecount<gTotalNumberOfNodes;linecount++)
	{
		//strcpy(line,strtok_r(NULL,"\n"));
		strcpy(line,strtok(NULL,"\n"));
		int16_t id,port;
		char IP[50];
		/*char* tok;
		tok=strtok_r(line," ",&saveptr2);
		sscanf(tok,"%i",&id);
		//id=atoi(tok);
		tok=strtok_r(NULL," ",&saveptr2);
		strcpy(IP,tok);


		tok=strtok_r(NULL," ",&saveptr2);
		sscanf(tok,"%i",&port);*/


		sscanf(line,"%hd%s%hd",&id,IP,&port);
		struct in_addr IP_Net;
		inet_aton(IP,&IP_Net);
		//port=atoi(tok);
		//if(id==(int16_t)baseId) // add at 0th index
		if(strcmp(gLocalIP,IP)==0)
		{
			//strcpy(costList[0].IP,IP);
			costList[0].IP=IP_Net.s_addr;
			costList[0].id=id;
			costList[0].nextHopId=-1;
			costList[0].port=port;
			gLocalPort=(int16_t)port;
			costList[0].cost=(int16_t)0;
			//testing
			//strcpy(gLocalIP,"");

			distanceVector[0].IP=IP_Net.s_addr;
			distanceVector[0].id=id;
			distanceVector[0].nextHopId=0;
			distanceVector[0].port=port;
			distanceVector[0].cost=0;
			costMatrix[0][0]=0;
		}
		else
		{
			//strcpy(costList[i].IP,IP);
			costList[i].IP=IP_Net.s_addr;
			costList[i].id=id;
			costList[i].nextHopId=-1;
			costList[i].port=port;


			distanceVector[i].IP=IP_Net.s_addr;
			distanceVector[i].id=id;
			distanceVector[i].port=port;
			//distanceVector[i].nextHopId=id;
			i++;
		}
	}
	//saveptr2=NULL;
// add the costs to the costList array
	for(int i=0;i<gNumberOfNeighbours;i++)
	{
		//change handle source !=localid case
			//strcpy(line,strtok_r(NULL,"\n",&saveptr1));
		strcpy(line,strtok(NULL,"\n"));
		int32_t  source,dest,cost;
			/*char IP[50] ;
			char* tok;
			tok =strtok_r(line," ",&saveptr2);
			source=atoi(tok);
			tok=strtok_r(NULL," ",&saveptr2);
			//dest=atoi(tok);
			sscanf(tok,"%i",&dest);

			tok=strtok_r(NULL," ",&saveptr2);
			sscanf(tok,"%i",&cost);*/

			sscanf(line,"%hd%hd%hd",&source, &dest,&cost);
			//cost=atoi(tok);
			int index=indexOfNodeWithId(dest);
			if(index<0) // should never happen
				continue;
			costList[index].cost= cost;
			costList[index].isActiveNeighbour=true;
		}
	}
	catch(...)
	{
		printf("Invalid topology file. Exiting...\n");
		fflush(stdout);
		exit(0);
	}
	updateSelfDistanceVectorWithVector(true);

}

// Parses the command line arguments and sets up the global variables accordingly.
void parseShellArguments(int argc,char *argv[])
{
	int index=1;
	try
	{
	while(index<argc)
	{
		if(strcmp(argv[index],"-t")==0)
		{
			strcpy(gTopologyFileName,argv[++index]);
			index++;
		}
		else if(strcmp(argv[index],"-i")==0)
		{
			gUpdateInterval=atoi(argv[++index]);
			index++;
		}
		else
		{
			printf("Unknown option");
			exit(0);
		}
	}
	}
	catch(...)
	{
		printf("Invalid usage\n");
		exit(0);
	}
}


void resetToPrompt()
{

	printf(PROMPTVAR);
	fflush(stdout);
}

// Handles the queries and exchange commands from the user.
void displayShell()
{
	char inputLine[1024];

	//fflush(stdout);
	fgets(inputLine,1024,stdin);
	char *command;//[30];
	command=strtok(inputLine,"\n");
	command=strtok(inputLine," ");
	//	strcpy(command,strtok(inputLine,"\n"));
	//strcpy(command,strtok(inputLine," "));
	//printf("command:%s\n",command);
	if(!command)
	{
		resetToPrompt();
		return;
	}

	if(!strcmp(command,"step"))
	{
		broadcastDistanceVectorToNeighbours(false);
		printf("step SUCCESS\n");
		timeout.tv_sec=gUpdateInterval;
		timeout.tv_usec=0;
		resetToPrompt();
	}

	else if(!strcmp(command,"MYIP"))
	{
		printf("%s\n",gLocalIP);
		resetToPrompt();
	}
	else if(!strcmp(command,"display"))
	{
		printf("display: SUCCESS\n");
		displayRoutingTable();
		resetToPrompt();
	}

	else if(!strcmp(command,"packets"))
	{
		printf("packets: SUCCESS\n");
		printf("Number of distance vector updates received:%d\n",gUpdateCount);
		gUpdateCount=0;
		resetToPrompt();

	}
	else if(!strcmp(command,"crash"))
	{
		printf("Server crashed\n");
		while(1);

	}
	else if(!strcmp(command,"update"))
	{
		try {
		int16_t id1=0,id2=0,c=0;
		char cost[8]="";

		char *t=strtok(NULL," ");
		if(t==NULL)
		{
			printf("Invalid usage.\n");
			resetToPrompt();
			return;
		}
		sscanf(t,"%hd",&id1);

		t=strtok(NULL," ");
		if(t==NULL)
		{
			printf("Invalid usage.\n");
			resetToPrompt();
			return;
		}
		sscanf(t,"%hd",&id2);

		t=strtok(NULL," ");
		if(t==NULL)
		{
			printf("Invalid usage.\n");
			resetToPrompt();
			return;
		}

		sscanf(t,"%s",&cost);

		if(id1<=0||id2<=0||strlen(cost)==0)
		{
			printf("update: FAILURE: Invalid or empty parameters.\n");
			resetToPrompt();
			return;
		}
		if(strcmp(cost,"inf")==0)
			c=-1;
		else
			sscanf(cost,"%hd",&c);
		int i=indexOfNodeWithId(id1);
		if(i!=0)
		{
			printf("update: FAILURE: Server id1 is not the local host.\n");
			resetToPrompt();
			return;
		}
		int index=indexOfNodeWithId(id2);
		if(index==-1)
		{
			printf("update: FAILURE: Server id2 is not a valid id.\n");
			resetToPrompt();
			return;
		}
		costList[index].cost=c;
		costList[index].isActiveNeighbour=true;
		costList[index].updateCount=MAXRETRY;
		//printf("setting costlist[%d]:%hd",index,c);
//		distanceVector[index].cost=-1;
//		costMatrix[0][index]=-1;
		resetDistanceVector();
		updateSelfDistanceVectorWithVector(false);
		printf("update: SUCCESS\n");
		resetToPrompt();
		}
		catch(...)
		{
			printf("Invalid usage.\n");
			resetToPrompt();
			return;
		}
	}
	else if(!strcmp(command,"disable"))
	{
		try {
		int16_t id=0;
		char* t=strtok(NULL," ");
		if(t==NULL)
		{
			printf("Invalid usage.\n");
			resetToPrompt();
			return;
		}
		sscanf(t,"%hd",&id);


		int index=indexOfNodeWithId(id);
		if(index<0||costList[index].cost==-1)
		{
			printf("disable: FAILED since server id is not a neighbour\n");
			resetToPrompt();
		}
		else
		{
			costList[index].cost=-1;
//			distanceVector[index].cost=-1;
//			costMatrix[0][index]=-1;
			resetDistanceVector();
			costList[index].isActiveNeighbour=false;
			updateSelfDistanceVectorWithVector(false);
			printf("disable: SUCCESS\n");
			resetToPrompt();
		}
		}
		catch(...)
		{
			printf("Invalid usage.\n");
			resetToPrompt();
			return;
		}
	}

	/*else if(!strcmp(command,"CONNECT"))
	{
		if(isServer)
		{
			printf("Invalid Command\n");
			resetToPrompt();
			return;
		}
		char *peerIP;//[40];
				//strcpy(tempServerIP,strtok(NULL," "));
		peerIP=strtok(NULL," ");
		char *portStr;//[5];
		portStr=strtok(NULL," ");
		//if(!strlen(peerIP)||strlen(portStr))
		if(!peerIP||!portStr|| !strlen(peerIP)||!strlen(portStr))
			printf("Invalid Command Format.\n");
		else
		{
			// check for the validity of the connection
			char *fIP=(char*)malloc(40);
			int fPort;
			char *errString=(char*)malloc(500);
			bool status=completeRequestAndCheckValidity(peerIP,portStr,&fPort,&fIP,&errString);
			if(!status)
			{
				printf("%s",errString);
				free(errString);
				resetToPrompt();
				return;
			}
			printf("Trying to connect to %s at port %d\n",fIP,fPort);
			TCPSocketConnection *peerConnectConn=new TCPSocketConnection(fIP,fPort,0);
			char connectReqMessage[1024];
			sprintf(connectReqMessage,"ConnectRequest\n%s\n%d\n%s\n",hostIP,localPort,localHostName);
			peerConnectConn->sendData(connectReqMessage);
			addConnection(peerConnectConn);
			free(fIP);
			//resetToPrompt();

		}

	}
	else if(!strcmp(command,"LIST"))
	{
		listConnections();
		resetToPrompt();

	}

	else if(!strcmp(command,"DOWNLOAD"))
	{
		if(isServer)
		{
			printf("Invalid Command\n");
			resetToPrompt();
			return;
		}

		char *fileName=strtok(NULL," ");
		char *chunkSizeStr=strtok(NULL," ");
		if(!fileName||!chunkSizeStr|| !strlen(fileName)||!strlen(chunkSizeStr))
			printf("Invalid Command Format.\n");
		else
		{
			downloadActive=true;
			lastWrittenByte=0;
			downloadFile(fileName,atoi(chunkSizeStr),-1,NULL);
		}
		printf("Downloading %s...\n",fileName);
		resetToPrompt();
	}

	else if(!strcmp(command,"TERMINATE"))
	{
		char *idStr;
		idStr=strtok(NULL," ");
		if(strlen(idStr)==0)
			printf("Invalid Usage.\n");
		else
		{
			if(terminateConnection(atoi(idStr),false))
				printf("Connection Terminated.\n");
			else
				printf("Operation not permitted.\n");
		}


		resetToPrompt();
	}
	else if(!strcmp(command,"EXIT"))
	{
		terminateAllConnections();
		//printf("\n*** Transferred %lld bytes in %lld us\n",totalBytesTransferred,totalTimeInMicroSecs);
		exit(0);
	}

	else if(!strcmp(command,"SIP"))
	{
		displayServerIPList();
		resetToPrompt();
	}
	else if(!strcmp(command,"CREATOR"))
	{
		printf("Name:Subhranil Banerjee\nUBITName:subhrani\nEmail:subhrani@buffalo.edu\n");
		resetToPrompt();
	}
*/
	else
	{
		printf("Command not found.\n");
		resetToPrompt();
	}

	//}

}


// Receives data on the UDP socket. Also contains the implementation of the timer.
void setupReceiver()
{
	//int sendFileDesc,recvFileDesc;

	/* observedSockets file descriptor list and max value */
	fd_set observedSockets;
	int fdmax;
	int mainSocket;
	// defining the address for the main socket

	SocketAddressInfo *socketAddress; // this structure basically stores info about the socket address
	socketAddress=(SocketAddressInfo*)malloc(sizeof(SocketAddressInfo));
	socketAddress->sin_family=AF_INET;

	inet_pton(socketAddress->sin_family,gLocalIP,&(socketAddress->sin_addr));
	socketAddress->sin_port=htons(gLocalPort);

	//create a socket for accepting incoming connections

	mainSocket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if(mainSocket==-1)
	{
		printf("Error in creating socket");
		exit(0);
	}

	// bind the socket to the addressinfo

	if(bind(mainSocket,(struct sockaddr*)socketAddress,sizeof(SocketAddressInfo))==-1)
	{
		printf("Error in binding socket IP address");
		exit(0);
	}

	// Begin code for socket listening

	/* clear the observedSockets and temp sets */
	FD_ZERO(&observedSockets);


	timeout.tv_sec=gUpdateInterval;
	timeout.tv_usec=0;
	// run loop for observing sockets

	while(true)
	{
		/* add the listener to the observedSockets set */
		FD_SET(mainSocket, &observedSockets);
		FD_SET(0,&observedSockets);

		/* keep track of the biggest file descriptor */
		fdmax = mainSocket ; /* so far, it's this one*/


		int activity=select(fdmax+1,&observedSockets,NULL,NULL,&timeout); // blocking all until there is some activity on any of the sockets
		if(activity<0)
		{
			if(errno==EINTR)
				continue;
			printf("error in select");
			return ;
		}
		else if(activity==0)
		{
			broadcastDistanceVectorToNeighbours(true);
			timeout.tv_sec=gUpdateInterval;
			timeout.tv_usec=0;
		}
		else {
		if(FD_ISSET(mainSocket,&observedSockets)) // .. there has been activity on the mainSocket. thus there s a new connection that needs to be added
		{
			char incomingMessage[MAXPACKETSIZE];
			int numberOfBytesRecvd=0;
			numberOfBytesRecvd=recv(mainSocket,&incomingMessage,MAXPACKETSIZE-1,0);

			//Handle the data
			int32_t id;
			Node* incomingVector=convertMessageIntoDistanceVector(incomingMessage,&id);


			//resetToPrompt();
			updateSelfDistanceVectorWithVector(false);

		}
		else if(FD_ISSET(0,&observedSockets))
		{
					displayShell();
		}
		}
	}

}
int main(int argc,char *argv[])
{
	init();
	parseShellArguments(argc,argv);
	strcpy(gLocalIP,getLocalIP());
	initialiseLists();
	resetToPrompt();
	setupReceiver();
}




