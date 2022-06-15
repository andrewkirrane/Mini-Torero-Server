/**
 * ToreroServe: A Lean Web Server
 * COMP 375 - Project 02
 *
 * This program should take two arguments:
 * 	1. The port number on which to bind and listen for connections
 * 	2. The directory out of which to serve files.
 *
 * Author 1: Andrew Kirrane
 * Author 2: Ajay Samra
 */

// standard C libraries
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

// operating system specific libraries
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <pthread.h>

// C++ standard libraries
#include <vector>
#include <thread>
#include <string>
#include <iostream>
#include <system_error>
#include <filesystem>
#include <regex>
#include <fstream>

#include "BoundedBuffer.hpp"

#define BUFFER_SIZE 2048
#define TRANSACTION_CLOSE 2

// shorten the std::filesystem namespace down to just fs
namespace fs = std::filesystem;

using std::cout;
using std::string;
using std::vector;
using std::thread;

// This will limit how many clients can be waiting for a connection.
static const int BACKLOG = 10;
const size_t CAPACITY = 10;
const size_t NUM_THREADS = 8;

// forward declarations
int createSocketAndListen(const int port_num);
void acceptConnections(const int server_sock, std::string root);
void handleClient(const int client_sock, std::string root);
void sendData(int socked_fd, const char *data, size_t data_length);
int receiveData(int socked_fd, char *dest, size_t buff_size);
void consume(BoundedBuffer &buffer, std::string root);
bool isDirectory(std::string filename);
bool fileExists(std::string filename);
bool validGET(std::string request);
void sendBad(const int client_sock);
void sendNotFound(const int client_sock);
void sendOK(const int client_sock);
void sendError(const int client_sock);
void sendHeader(const int client_sock, std::string filename);
void sendHTML(const int client_sock, std::string filename);
void sendFile(const int client_sock, std::string filename);

int main(int argc, char** argv) {

	/* Make sure the user called our program correctly. */
	if (argc != 3) {
		cout << "INCORRECT USAGE!\n";
		cout << "Format: './(compiled exec) (port num) (root directory)'\n";
		exit(1);
	}

	/* Read the port number from the first command line argument. */
	int port = std::stoi(argv[1]);

	// Read the root directory from the command line
	std::string root = (argv[2]);

	/* Create a socket and start listening for new connections on the
	 * specified port. */
	int server_sock = createSocketAndListen(port);

	/* Now let's start accepting connections. */
	acceptConnections(server_sock, root);
	
	// Close socket
	close(server_sock);

	return 0;
}

/**
 * Sends message over given socket, raising an exception if there was a problem
 * sending.
 *
 * @param socket_fd The socket to send data over.
 * @param data The data to send.
 * @param data_length Number of bytes of data to send.
 */
void sendData(int socked_fd, const char *data, size_t data_length) {
	while(data_length > 0) {
		int num_bytes_sent = send(socked_fd, data, data_length, 0);
		if (num_bytes_sent == -1) {
			std::error_code ec(errno, std::generic_category());
			throw std::system_error(ec, "send failed");
		}
		data_length -= num_bytes_sent; // adjust send size
		data += num_bytes_sent; // find next position to add to buffer
	}
}

/**
 * Receives message over given socket, raising an exception if there was an
 * error in receiving.
 *
 * @param socket_fd The socket to send data over.
 * @param dest The buffer where we will store the received data.
 * @param buff_size Number of bytes in the buffer.
 * @return The number of bytes received and written to the destination buffer.
 */
int receiveData(int socked_fd, char *dest, size_t buff_size) {
	int num_bytes_received = recv(socked_fd, dest, buff_size, 0);
	if (num_bytes_received == -1) {
		std::error_code ec(errno, std::generic_category());
		throw std::system_error(ec, "recv failed");
	}

	return num_bytes_received;
}

/**
 * Receives a request from a connected HTTP client and sends back the
 * appropriate response.
 *
 * @note After this function returns, client_sock will have been closed (i.e.
 * may not be used again).
 *
 * @param client_sock The client's socket file descriptor.
 * @param root The directory root name
 */
void handleClient(const int client_sock, std::string root) {
	// Step 1: Receive the request message from the client
	char received_data[BUFFER_SIZE];
	int bytes_received = receiveData(client_sock, received_data, BUFFER_SIZE);

	// Turn the char array into a C++ string for easier processing.
	string request_string(received_data, bytes_received);

	if(!validGET(request_string)) { // test for bad request
		sendBad(client_sock);
		return;
	}
	// tokenize path
	std::istringstream f(request_string);
	std::string filename;
	getline(f, filename, ' ');
	getline(f, filename, ' ');
	
	root.append(filename); // find directory with root

	if(!fileExists(root) && !isDirectory(root)) { // test for valid directory/file
		sendNotFound(client_sock);
		sendError(client_sock);
		return;
	}
	
	// generates HTTP response based on request
	// response is split into header and data
	sendOK(client_sock);

	if(isDirectory(root)) { // send the HTML data for the directory
		sendHTML(client_sock, root);
	}
	else if(fileExists(root)) { // send header and file data for file request
		sendHeader(client_sock, root);
		sendFile(client_sock, root);
	}
	// Close connection with client.
	close(client_sock);
}

/**
 * Creates a new socket and starts listening on that socket for new
 * connections.
 *
 * @param port_num The port number on which to listen for connections.
 * @returns The socket file descriptor
 */
int createSocketAndListen(const int port_num) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("Creating socket failed");
		exit(1);
	}

	/* 
	 * A server socket is bound to a port, which it will listen on for incoming
	 * connections.  By default, when a bound socket is closed, the OS waits a
	 * couple of minutes before allowing the port to be re-used.  This is
	 * inconvenient when you're developing an application, since it means that
	 * you have to wait a minute or two after you run to try things again, so
	 * we can disable the wait time by setting a socket option called
	 * SO_REUSEADDR, which tells the OS that we want to be able to immediately
	 * re-bind to that same port. See the socket(7) man page ("man 7 socket")
	 * and setsockopt(2) pages for more details about socket options.
	 */
	int reuse_true = 1;

	int retval; // for checking return values

	retval = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true,
			sizeof(reuse_true));

	if (retval < 0) {
		perror("Setting socket option failed");
		exit(1);
	}

	/*
	 * Create an address structure.  This is very similar to what we saw on the
	 * client side, only this time, we're not telling the OS where to connect,
	 * we're telling it to bind to a particular address and port to receive
	 * incoming connections.  Like the client side, we must use htons() to put
	 * the port number in network byte order.  When specifying the IP address,
	 * we use a special constant, INADDR_ANY, which tells the OS to bind to all
	 * of the system's addresses.  If your machine has multiple network
	 * interfaces, and you only wanted to accept connections from one of them,
	 * you could supply the address of the interface you wanted to use here.
	 */
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_num);
	addr.sin_addr.s_addr = INADDR_ANY;

	/* 
	 * As its name implies, this system call asks the OS to bind the socket to
	 * address and port specified above.
	 */
	retval = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (retval < 0) {
		perror("Error binding to port");
		exit(1);
	}

	/* 
	 * Now that we've bound to an address and port, we tell the OS that we're
	 * ready to start listening for client connections. This effectively
	 * activates the server socket. BACKLOG (a global constant defined above)
	 * tells the OS how much space to reserve for incoming connections that have
	 * not yet been accepted.
	 */
	retval = listen(sock, BACKLOG);
	if (retval < 0) {
		perror("Error listening for connections");
		exit(1);
	}

	return sock;
}

/**
 * Sit around forever accepting new connections from client.
 *
 * @param server_sock The socket used by the server.
 * @param root The directory root name
 */
void acceptConnections(const int server_sock, std::string root) {
	BoundedBuffer buffer(CAPACITY);
	for(size_t i = 0; i < NUM_THREADS; i++) { // creates threads based on NUM_THREADS (8)
		std::thread cons(consume, std::ref(buffer), root);
		cons.detach();
	}

	while (true) {
		// Declare a socket for the client connection.
		int sock;

		/* 
		 * Another address structure.  This time, the system will automatically
		 * fill it in, when we accept a connection, to tell us where the
		 * connection came from.
		 */
		struct sockaddr_in remote_addr;
		unsigned int socklen = sizeof(remote_addr); 

		/* 
		 * Accept the first waiting connection from the server socket and
		 * populate the address information.  The result (sock) is a socket
		 * descriptor for the conversation with the newly connected client.  If
		 * there are no pending connections in the back log, this function will
		 * block indefinitely while waiting for a client connection to be made.
		 */
		sock = accept(server_sock, (struct sockaddr*) &remote_addr, &socklen);
		if (sock < 0) {
			perror("Error accepting connection");
			exit(1);
		}

		/* 
		 * At this point, you have a connected socket (named sock) that you can
		 * use to send() and recv(). The handleClient function should handle all
		 * of the sending and receiving to/from the client.
		 */
		buffer.putItem(sock);
	}
}

/**
 * Allows threads to wait until a client socket is ready
 * handleClient is called when socket is ready.
 * Producer threads add to buffer
 * Consumer threads take out of buffer
 *
 * @param buffer An instance of the BoundedBuffer class that is shared by
 * threads
 * @param root The directory root name
 */
void consume(BoundedBuffer &buffer, std::string root) {
	while(true) {
		int shared_socket = buffer.getItem(); // buffer has shared socket
		handleClient(shared_socket, root); // handleClient is called when a client socket is ready
	}
}

/**
 * Check for valid HTTP GET request
 *
 * @param request Request message from client
 * @returns true GET request is valid
 */
bool validGET(std::string request) {
	// checks for GET regex pattern
	std::regex http_request_regex("(GET\\s[\\w\\-\\./]*\\sHTTP/\\d\\.\\d)");
	std::smatch match;

	if(std::regex_search(request, match, http_request_regex)) { // if valid request
		return true;
	}
	else {
		return false;
	}
}

/**
 * Check for file in the root directory
 *
 * @param filename Requested file
 * @returns true File exists in root directory
 */
bool fileExists(std::string filename) {
	std::ifstream f(filename.c_str());
	return f.good(); // returns true if no errors in ifstream
}

/**
 * Check if requested directory exists and is in the root directory
 *
 * @param filename Requested directory
 * @returns true Directory exists and ends in '/'
 */
bool isDirectory(std::string filename) {
	return (fs::is_directory(filename));
}

/**
 * Send an HTTP 400 BAD REQUEST response
 *
 * @param client_sock Client's socket file descriptor
 */
void sendBad(const int client_sock) {
	std::string request = "HTTP/1.0 400 BAD REQUEST\r\n";
	sendData(client_sock, request.c_str(), request.length()); // send response to client
}

/**
 * Send an HTTP 404 NOT FOUND response
 *
 * @param client_sock Client's socket file descriptor
 */
void sendNotFound(const int client_sock) {
	std::string request = "HTTP/1.0 404 NOT FOUND\r\n";
	sendData(client_sock, request.c_str(), request.length()); // send response to client
}

/**
 * Send an HTTP 200 OK response
 *
 * @param client_sock Client's socket file descriptor
 */
void sendOK(const int client_sock) {
	std::string request = "HTTP/1.0 200 OK\r\n";
	sendData(client_sock, request.c_str(), request.length()); // send response to client
}

/**
 * Send requested file data
 *
 * @param client_sock Client's socket file descriptor
 * @param filename Requested file
 */
void sendFile(const int client_sock, std::string filename) {
	std::ifstream file(filename, std::ios::binary); // open file
	const unsigned int buff_size = 4096;
	char data[buff_size]; // create buffer to store data

	while(!file.eof()) { // read through file, capturing contents
		file.read(data, buff_size);
		int bytes = file.gcount();
		sendData(client_sock, data, bytes); // send file data to client
	}
	file.close(); // close file
	sendData(client_sock, "\r\n", TRANSACTION_CLOSE); // tell client that we are done sending
}

/**
 * Send HTTP headers without data
 *
 * @param client_sock Client's socket file descriptor
 * @param filename Requested file
 */
void sendHeader(const int client_sock, std::string filename) {
	std::regex rgx("(\\.\\w*)"); // regex to check for file extension
	std::smatch match;
	std::string type;
	std::string size;

	// match extension against most common file types
	if(std::regex_search(filename, match, rgx)) {
			if(match[0] == ".html") { // match against html
			type = "text/html";
			}
			else if(match[0] == ".css") { // match against css
			type = "text/css";
			}
			else if(match[0] == ".jpg") { // match against jpg
			type = "image/jpeg";
			}
			else if(match[0] == ".gif") { // match against gif
			type = "image/gif";
			}
			else if(match[0] == ".png") { // match against png
			type = "image/png";
			}
			else if(match[0] == ".pdf") { // match against pdf
			type = "application/pdf";
			}
			else { // assume txt file
			type = "text/plain";
			}
	}
	else { // if no match
		std::cout << "No Matches!";
		return;
	}

	// capture file type and content length, send information back to client
	std::stringstream stream;
	stream << "Content-Type: " << type << "\r\n"
		<< "Content-Length: " << std::to_string(fs::file_size(filename)) << "\r\n"
		<< "\r\n";

	std::string response = stream.str();
	sendData(client_sock, response.c_str(), response.length());
}

/**
 * Generate HTML file that lists files or directories inside of specified
 * directory
 *
 * @param client_sock Client's socket file descriptor
 * @param filename Requested file
 */
void sendHTML(const int client_sock, std::string filename) {
	std::stringstream stream;

	if(!isDirectory(filename) && fileExists(filename)) { // if file or directory is not found sned error page
		stream << "<html>" << "\r\n"
			<< "<head>" << "\r\n"
			<< "<title> Page not found! </title>" << "\r\n"
			<< "</head>" << "\r\n"
			<< "<body> 404 Page Not Found! </body>" << "\r\n"
			<< "</html>" << "\r\n";

		std::string error_page = stream.str();
		std::stringstream resp;
		resp << "Content-Type: " << "text/html" << "\r\n"
			<< "Content-Length: " << error_page.length() << "\r\n"
			<< "\r\n" << error_page << "\r\n";

		std::string sendPage = resp.str();
		sendData(client_sock, sendPage.c_str(), sendPage.length()); // send error page to client to display
		return;
	}
	// generate HTML directory page
	stream << "<html>" << "\r\n"
		<< "<head>" << "<title></title>" << "</head>" << "\r\n"
		<< "<body>" << "\r\n"
		<< "<ul>" << "\r\n";
	
	// for each file entry in the specified directory
	for(auto& item: fs::directory_iterator(filename)) {
		if(item.path().filename() == "index.html") { // check for 'index.html' first and return this automatically if there is a match
			sendHeader(client_sock, item.path());
			sendFile(client_sock, item.path());
			return;
		}
		// check filenames and add all files
		else if(fs::is_regular_file(filename + item.path().filename().string())) {
			stream << "\t<li><a href=\"" << item.path().filename().string() << "\">" << item.path().filename().string() << "</a></li>\r\n";
		}
		else if(fs::is_directory(filename + item.path().filename().string())) {
			stream << "\t<li><a href=\"" << item.path().filename().string() << "/\">" << item.path().filename().string() << "/</a></li>\r\n";
		}
	}	
	stream << "</ul>" << "\r\n"
		<< "</body>" << "\r\n"
		<< "</html>" << "\r\n";

	std::string html_page = stream.str();
	std::stringstream resp2;

	// create header info
	resp2 << "Content-Type: " << "text/html" << "\r\n"
		<< "Content-Length: " << html_page.length() << "\r\n"
		<< "\r\n" << html_page << "\r\n";

	std::string sendPage2 = resp2.str();
	sendData(client_sock, sendPage2.c_str(), sendPage2.length()); // send content type and length to client
}

/**
 * Send error page to client if their request cannot be found
 *
 * @param client_sock Client's socket file descriptor
 */
void sendError(const int client_sock) {
	std::cout << "Error!!!";
	std::stringstream stream;
	// create error page
	stream << "<html>" << "\r\n"
           << "<head>" << "\r\n"
           << "<title> Page not found! </title>" << "\r\n"
           << "</head>" << "\r\n"
           << "<body> 404 Page Not Found! </body>" << "\r\n"
           << "</html>" << "\r\n";
	
	std::string error_page = stream.str();
	std::stringstream resp;
	// create package for error page
	resp << "Content-Type: " << "text/html" << "\r\n"
         << "Content-Length: " << error_page.length() << "\r\n"
         << "\r\n" << error_page << "\r\n";

	std::string sendPage = resp.str();
	sendData(client_sock, sendPage.c_str(), sendPage.length()); // send error page to client
	return;
}
