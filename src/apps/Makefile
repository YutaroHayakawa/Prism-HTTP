SUBDIRS:=bench kvs

all:
	for subdir in $(SUBDIRS); do \
		make -C $${subdir} all; \
	done

install:
	for subdir in $(SUBDIRS); do \
		make -C $${subdir} install; \
	done

clean:
	for subdir in $(SUBDIRS); do \
		make -C $${subdir} clean; \
	done
