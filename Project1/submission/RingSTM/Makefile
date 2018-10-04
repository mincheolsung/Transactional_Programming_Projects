include Makefile.defines.in

CCFLAGS += -std=c++11

OBJFILES = $(OBJ_DIR)/WriteSet.o $(OBJ_DIR)/ring_t.o $(OBJ_DIR)/test_t.o

.PHONY: clean

all:  $(OBJ_DIR)/test_threads 

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/test_threads: $(OBJFILES)
	$(CPP) $(CCFLAGS) -o $@ $(OBJFILES) $(LDFLAGS)
	cp $(OBJ_DIR)/test_threads .


$(OBJ_DIR)/test_t.o: $(OBJ_DIR) $(SRC_DIR)/test_threads.cpp 
	$(CPP) $(CCFLAGS) $(CPPFLAGS) $(SRC_DIR)/test_threads.cpp -c -o $@


$(OBJ_DIR)/WriteSet.o: $(OBJ_DIR) $(SRC_DIR)/tm/WriteSet.c $(SRC_DIR)/tm/WriteSet.hpp
	$(CPP) $(CCFLAGS) $(CPPFLAGS) $(SRC_DIR)/tm/WriteSet.c -c -o $@

$(OBJ_DIR)/ring_t.o: $(OBJ_DIR) $(SRC_DIR)/tm/ring_stm.c $(SRC_DIR)/tm/ring_stm.hpp
	$(CPP) $(CCFLAGS) $(CPPFLAGS) $(SRC_DIR)/tm/ring_stm.c -c -o $@


################
# common tasks #
################

clean:
	rm -rf $(TARGET_DIR)
	rm -f test_threads


