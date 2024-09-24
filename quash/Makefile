STUDENT_ID=3041677

quash:
	gcc -Wall -g quash.c -o quash

test: clean quash
	./quash
	rm -f quash

clean:
	rm -f quash

update: clean quash

tar:
	make clean
	mkdir $(STUDENT_ID)-quash
	cp -r Makefile quash.c $(STUDENT_ID)-quash
	tar cvzf $(STUDENT_ID)-quash.tar.gz $(STUDENT_ID)-quash
	rm -rf $(STUDENT_ID)-quash
