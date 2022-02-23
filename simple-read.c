#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <liburing.h>
#include <fcntl.h>

#define QUEUE_DEPTH 16
#define BLOCK_SZ 4096
#include <sys/ioctl.h>

/*
 * Output a string of characters of len length to stdout.
 * We use buffered output here to be efficient,
 * since we need to output character-by-character.
 * */
void output_to_console(char *buf, int len) {
    while (len--) {
        fputc(*buf++, stdout);
    }
}


off_t get_file_size(int fd) {
    struct stat st;

    if(fstat(fd, &st) < 0) {
        perror("fstat");
        return -1;
    }

    if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
            perror("ioctl");
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}




int submit_read_request(int fd, struct io_uring *ring) {
	int stat = 0;
//	off_t file_size = get_file_size(fd);
//	off_t bytes_to_go = file_size;
	static long current_block = 0;
//	long blocks_to_read = (int) file_size/BLOCK_SZ;
//	if (file_size % BLOCK_SZ) blocks_to_read++;
	off_t offset = current_block * BLOCK_SZ;
	void * buf;
	if( posix_memalign(&buf, BLOCK_SZ, BLOCK_SZ)) {
            perror("posix_memalign");
            return 1;
        }

    	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	io_uring_prep_read(sqe,fd,buf,BLOCK_SZ,offset);
	io_uring_submit(ring);
	fprintf(stdout,"sqe %ld offset: %ld  \n",current_block, offset);
	current_block++;
	return stat;
}

int get_read_completion(struct io_uring * ring) {
	struct io_uring_cqe *cqe;
	int ret = io_uring_wait_cqe(ring, &cqe);
	if (ret < 0) {
		perror("io_uring_wait_cqe");
		return 1;
	}
	fprintf(stdout,"cqe result: %d\n",cqe->res);
	io_uring_cqe_seen(ring, cqe);
	return cqe->res;
}


int main (int argc, char**argv) {
	int stat;
	struct io_uring ring;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [file name]\n", argv[0]);
		return 1;
	}
	if (stat = io_uring_queue_init(QUEUE_DEPTH, &ring, 0)) {
		fprintf(stderr,"Error io_uring init %d\n", stat);
		return -1;
	}

	int file_fd = open(argv[1], O_RDONLY|O_DIRECT);
	if (file_fd < 0) {
		fprintf(stderr,"failed to open %s %d\n", argv[1], file_fd);
		return -1;
	}
	off_t file_size = get_file_size(file_fd);
	printf("%s size: %jd\n", argv[1], (intmax_t)file_size);
	
	while(1) {
		submit_read_request(file_fd, &ring);
		stat = get_read_completion( &ring);
		if (stat < BLOCK_SZ) break;
	}

}
