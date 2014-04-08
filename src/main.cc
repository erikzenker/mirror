#include <Inotify.h>
#include <FileSystemEvent.h>
#include <FileExchangeSocket.h>

#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <tuple>
#include <exception>

using boost::asio::ip::tcp;

void stringToFile(const std::string s, const boost::filesystem::path path){
    std::ofstream file;
    file.open(path.c_str());
    file << s;
    file.close();

}

void recvLoop(const boost::filesystem::path rootPath, const unsigned port, Inotify &inotify){

  while(true){
    // Receive message
    std::string header, body;
    std::tie(header, body) = recvMessage(port);
    
    // Get message type
    std::stringstream headerStream;
    headerStream << header;
    boost::property_tree::ptree ptree;
    read_xml(headerStream, ptree);
    const std::string type = ptree.get<std::string>("transmission.type");

    // Switch on message type
    if(!type.compare("file")){
      // Write Content to file
      const std::string relativePath = ptree.get<std::string>("transmission.relativePath");
      inotify.ignoreFileOnce(relativePath);
      stringToFile(body, rootPath / relativePath);
      inotify.watchDirectoryRecursively(rootPath / relativePath);

    }
    else if(!type.compare("mkdir")){
      // Mkdir
      const std::string relativePath = ptree.get<std::string>("transmission.relativePath");
      inotify.ignoreFileOnce(relativePath);
      boost::filesystem::create_directories(rootPath / relativePath);
      inotify.watchDirectoryRecursively(rootPath / relativePath);

    }
  }

}


int main(int argc, char* argv[]){
  if(argc != 5){
    std::cerr << "Usage: ./mirror WATCHPATH HOST OTHERPORT OWNPORT" << std::endl;
    return 0;
  }

  const std::string watchPath = argv[1];
  const std::string otherHost = argv[2];
  const std::string otherPort = argv[3];
  const unsigned    ownPort   = atoi(argv[4]);

  // Watch directory
  Inotify inotify(IN_CLOSE_WRITE | IN_MOVE | IN_CREATE); 
  inotify.watchDirectoryRecursively(watchPath);

  // Start server for recv files
  std::thread waitForFiles(recvLoop, watchPath, ownPort, std::ref(inotify));

  // Wait for events
  while(true){
    FileSystemEvent event = inotify.getNextEvent();
    std::cerr << event.getMaskString() << " " << event.getMask()<<std::endl;
    switch(event.getMask()){
    case IN_MOVED_TO:
    case IN_CLOSE_WRITE :
      std::cerr << "Send file: " << event.getPath().string() << std::endl;
      try {
	sendFile(watchPath, event.getPath(), otherHost, otherPort);
      }
      catch(std::exception e){
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	sendFile(watchPath, event.getPath(), otherHost, otherPort);
      }
      break;

    case IN_CREATE | IN_ISDIR:
      mkdir(watchPath, event.getPath(), otherHost, otherPort);
      break;
      

    default:
      break;

    }
    
  }

  waitForFiles.join();
  return 0;

}
