make:
	gcc noncanonical.c connection.c application.c utils/state_machine.c utils/builders.c utils/strmanip.c -Wall -Wextra -pedantic -o noncanonical -D debug
