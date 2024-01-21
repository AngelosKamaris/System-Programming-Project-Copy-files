all: dataServer remoteClient

dataServer: dataServer.cpp 
	g++ -o dataServer dataServer.cpp -lpthread

remoteClient: remoteClient.cpp 
	g++ -o remoteClient remoteClient.cpp

clean:
	rm -f dataServer remoteClient