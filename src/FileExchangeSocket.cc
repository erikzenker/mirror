#include <FileExchangeSocket.h>

#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <sstream>
#include <tuple>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>


#define CHUNK_SIZE 8096

using boost::asio::ip::tcp;

/*************************************************************************
 *  UTILITIES                                                            *
 *                                                                       *
 *                                                                       *
 *************************************************************************/
boost::filesystem::path constructRelativePath(const boost::filesystem::path rootPath, boost::filesystem::path fullPath){
	return fullPath.string().substr(rootPath.string().length(), fullPath.string().length());
}

size_t fileSize(boost::filesystem::path path){
  std::ifstream file(path.c_str(), std::ios::in  | std::ios::binary | std::ios::ate);
  const size_t file_size  = file.tellg();
  file.close();

  return file_size;
}

/*************************************************************************
 *  PACKET CONTENTS                                                      *
 *                                                                       *
 *                                                                       *
 *************************************************************************/
std::string fileHeader(boost::filesystem::path rootPath, boost::filesystem::path path){
  const size_t file_size  = fileSize(path);

  std::stringstream header;
  header << "<transmission >" << std::endl;
  header << "<type>file</type>" << std::endl;
  header << "<filename>" << path.filename().string() << "</filename>" << std::endl;
  header << "<rootPath>" << rootPath.string() << "</rootPath>" << std::endl;
  header << "<path>" << path.parent_path().string() << "</path>" << std::endl;
  header << "<relativePath>" << constructRelativePath(rootPath, path).string() << "</relativePath>" << std::endl;
  header << "<size>" << file_size << "</size>" << std::endl;
  header << "<md5sum>" << "" << "</md5sum>" << std::endl;
  header << "</transmission>" << std::endl;

  return header.str();
}

std::string mkdirHeader(boost::filesystem::path rootPath, boost::filesystem::path path){
  std::stringstream header;
  header << "<transmission>" << std::endl;
  header << "<type>mkdir</type>" << std::endl;
  header << "<rootPath>" << rootPath.string() << "</rootPath>" << std::endl;
  header << "<path>" << path.string() << "</path>" << std::endl;
  header << "<relativePath>" << constructRelativePath(rootPath, path).string() << "</relativePath>" << std::endl;
  header << "<size>" << 0 << "</path>" << std::endl;
  header << "</transmission>" << std::endl;

  return header.str();
  
}

std::string fileBody(boost::filesystem::path path){
  std::ifstream file(path.c_str(), std::ios::in  | std::ios::binary | std::ios::ate);
  const size_t file_size  = file.tellg();

  char* memblock;
  if(file.is_open()){
    memblock = new char[file_size];
    file.seekg (0, std::ios::beg);
    file.read (memblock, file_size);
    file.close();
    std::string body = std::string (memblock, file_size); 
    delete[] memblock;
    return body;
    
  }

  return std::string("");
}


/*************************************************************************
 *  SEND FILE TOOLS                                                      *
 *                                                                       *
 *                                                                       *
 *************************************************************************/
void sendStream(tcp::socket &socket, std::istream &stream,  const size_t size){
  size_t transferred = 0;
  boost::array<char, CHUNK_SIZE> chunk;
    
  while (transferred != size){ 
      size_t remaining = size - transferred; 
      size_t write_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
      stream.read(&chunk[0], CHUNK_SIZE); 
      boost::asio::write(socket, boost::asio::buffer(chunk, write_size)); 
      transferred += write_size; 
    } 

}

void sendString(tcp::socket &socket, const std::string string){
  const size_t size = string.size();
  std::stringstream ss;
  ss << string;

  sendStream(socket, ss, size);
}

void sendFile(const boost::filesystem::path rootPath, const boost::filesystem::path path, const std::string host, const std::string port){
  // Connect to socket
  boost::asio::io_service io_service; 
  tcp::resolver resolver(io_service); 
  tcp::resolver::query url(host, port);
  tcp::resolver::iterator endpoint_iterator = resolver.resolve(url);
  tcp::socket socket(io_service);
  boost::asio::connect(socket, endpoint_iterator);

  // Prepare XML-Header and Body
  const std::string header = fileHeader(rootPath, path); 
  const std::string body   = fileBody(path);

  std::cout << "Sending to " 
  	    << socket.remote_endpoint().address() 
  	    << ":" 
  	    << socket.remote_endpoint().port() 
  	    << " " 
  	    << path.filename().string() 
  	    << " (" << body.size() << " Bytes)"<<std::endl;
  std::cout << header << std::endl;

  // Send header
  sendString(socket, header + body);

}

void mkdir(const boost::filesystem::path rootPath, const boost::filesystem::path path, const std::string host, const std::string port){
  // Connect to socket
  boost::asio::io_service io_service; 
  tcp::resolver resolver(io_service); 
  tcp::resolver::query url(host, port);
  tcp::resolver::iterator endpoint_iterator = resolver.resolve(url);
  tcp::socket socket(io_service);
  boost::asio::connect(socket, endpoint_iterator);

  std::cout << "Mkdir on " 
	    << socket.remote_endpoint().address() 
	    << ":" 
	    << socket.remote_endpoint().port() 
	    << " " 
	    << path.filename().string() << std::endl;

  const std::string header = mkdirHeader(rootPath, path);
  std::cout << header << std::endl;
  sendString(socket, header);
}

/*************************************************************************
 *  RECEIVE FILE TOOLS                                                   *
 *                                                                       *
 *                                                                       *
 *************************************************************************/
std::tuple<std::string, std::string> recvStringUntil(tcp::socket &socket, const std::string delim){
  bool foundDelim = false;
  boost::asio::streambuf streambuf;
  std::stringstream preStream;
  std::stringstream postStream;

  boost::asio::read_until(socket, streambuf, delim);
  std::istream is(&streambuf);

  while(is.good()){
    std::string string;
    std::getline(is,string);

    if(!foundDelim){
      const std::size_t pos = string.find(delim);
      if(pos != std::string::npos){
  	const std::size_t delimEndPos = pos + delim.size();
  	preStream << std::string(string.begin(), string.begin() + delimEndPos);
  	postStream << std::string(string.begin() + delimEndPos, string.end());
  	foundDelim = true;

      }
      else {
  	preStream << string << std::endl;

      }
      
    }
    else{
      postStream << string << std::endl;

    }

   }

  return std::make_tuple(preStream.str(), postStream.str());
}

std::string recvString(tcp::socket &socket){
  std::stringstream stream;
  boost::array<char, CHUNK_SIZE> buffer; 
  boost::system::error_code error;

  while(error != boost::asio::error::eof){
    memset(&buffer, 0, CHUNK_SIZE);
    size_t size = socket.read_some(boost::asio::buffer(buffer), error);
    stream.write(buffer.data(), size);
  }
  return stream.str();

}

std::string recvString(tcp::socket &socket, const size_t size){
  std::stringstream stream;
  boost::array<char, CHUNK_SIZE> chunk; 
  size_t transferred = 0;
    
  while (transferred != size) { 
    size_t remaining = size - transferred; 
    size_t read_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    boost::asio::read(socket, boost::asio::buffer(chunk, read_size)); 
    stream.write(&chunk[0], read_size); 
    transferred += read_size; 
  }

  return stream.str();

}

std::tuple<std::string, std::string> recvMessage(const unsigned port){
  // Create socket
  boost::asio::io_service io_service;
  tcp::endpoint endpoint(tcp::v4(), port);
  tcp::acceptor acceptor(io_service, endpoint); 

  // Accept connections to socket
  tcp::socket socket(io_service); 
  acceptor.accept(socket); 

  // Divide header and body
  std::string pre, post;
  std::tie(pre, post) = recvStringUntil(socket, "</transmission>");

  // Receive header
  std::stringstream header;
  header << pre;

  // Read Xml
  boost::property_tree::ptree ptree;
  read_xml(header, ptree);

  const size_t          size = ptree.get<size_t>("transmission.size");

  // Receive body (binary data)
  std::string body("");
  if(post.size() < size){
    body = body + post + recvString(socket, size - post.size());
  }
  else{
    body = body + post;
  }
  std::cout << "Received message from " 
	    << socket.remote_endpoint().address() 
	    << ":" 
	    << socket.remote_endpoint().port() 
	    << " " 
	    << " (" << header.str().size() + body.size() << " Bytes)" << std::endl;

  return std::make_tuple(header.str(), body);
}

std::tuple<std::string, std::string> recvFile(const unsigned port){
  // Receive message
  std::string header, body;
  std::tie(header, body) = recvMessage(port);

  std::stringstream headerStream;
  headerStream << header;

  // Get filename from xml
  boost::property_tree::ptree ptree;
  read_xml(headerStream, ptree);
  const std::string filename = ptree.get<std::string>("transmission.filename");

  return std::make_tuple(body, filename);

}



void asyncRecvFile(const unsigned port, void (*callback)(std::string, std::string, std::string)){


}
