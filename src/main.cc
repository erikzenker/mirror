#include <Inotify.h>
#include <FileSystemEvent.h>
#include <FileExchangeSocket.h>

#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
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
    std::string fileContent("");
    std::string filename("");
    std::tie(fileContent, filename) = recvFile(port);
      
    // Write Content to file
    inotify.ignoreFileOnce(filename);
    stringToFile(fileContent, rootPath / filename);
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
	sendFile(event.getPath(), otherHost, otherPort);
      }
      catch(std::exception e){
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	sendFile(event.getPath(), otherHost, otherPort);
      }
      break;
    // case IN_CREATE | IN_ISDIR:
    //   mkdir(watchPath, event.getPath(), otherHost, otherPort);

    default:
      break;

    }
    
  }

  waitForFiles.join();
  return 0;

}
