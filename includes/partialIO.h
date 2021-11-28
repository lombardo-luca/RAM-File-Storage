/* Tratto da:
* “Advanced Programming In the UNIX Environment” by W. Richard Stevens and Stephen A. Rago, 2013, 3rd Edition, Addison-Wesley
*
* We can use the readn and writen functions to read and write N bytes of data, respectively, 
* letting these functions handle a return value that’s possibly less than requested. 
* These two functions simply call read or write as many times as required to read or write the entire N bytes of data.
* We call writen whenever we’re writing to one of the file types that we mentioned, 
* but we call readn only when we know ahead of time that we will be receiving a certain number of bytes. 
* Note that if we encounter an error and have previously read or written any data, 
* we return the amount of data transferred instead of the error. 
* Similarly, if we reach the end of file while reading, we return the number of bytes copied to the caller’s buffer 
* if we already read some data successfully and have not yet satisfied the amount requested.
*/

/* Read "n" bytes from a descriptor */
ssize_t readn(int fd, void *ptr, size_t n);

/* Write "n" bytes to a descriptor */
ssize_t writen(int fd, void *ptr, size_t n);