all: data_gen_direct data_gen_host_cache knn_direct knn_host_only knn_hybrid #data_gen_direct data_gen_host_only

CODE=.
CXX=g++
CC=gcc
LIB_S=-lpthread -lrt #-lcrfslib #-L./libfs/interval_tree -lintervaltreelib
BUILD=./build

data_gen_direct: data_generator.cpp 
	g++ data_generator.cpp -std=c++11 -O3 -fPIC -shared -fopenmp -o $(BUILD)/libdata_gen.so $(LIB_S) -lcrfslib
	sudo cp $(BUILD)/*.so /usr/lib

data_gen_host_cache: data_generator.cpp 
	g++ data_generator.cpp -std=c++11 -O3 -fPIC -shared -fopenmp -o $(BUILD)/libdata_gen_host.so $(LIB_S) -lcrfslib_wb_host
	sudo cp $(BUILD)/*.so /usr/lib


#data_gen_host_only: data_generator.cpp 
#	g++ data_generator.cpp  -std=c++11 -O3 -fopenmp -o $(BUILD)/data_gen_host_only $(LIB_S) -lcrfslib_wb_host

knn_direct: knn.c
	$(CC) $(INCLUDE) $(FLAGS) knn.c  -o $(BUILD)/knn_direct $(LIB_S)  -lcrfslib -ldata_gen

knn_host_only: knn.c
	$(CC) $(INCLUDE) $(FLAGS) knn.c  -o $(BUILD)/knn_host_only $(LIB_S) -lcrfslib_wb_host -ldata_gen_host

knn_hybrid: knn.c
	$(CC) $(INCLUDE) $(FLAGS) knn.c  -o $(BUILD)/knn_hybrid $(LIB_S) -D_CACHE -lcrfslib_wb_hybrid -ldata_gen_host

clean:
	rm -rf *.o
	rm -rf *.so
	rm -rf *.so
	rm -rf $(BUILD)/*

