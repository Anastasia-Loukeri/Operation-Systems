# Ορισμός Μεταβλητών 
CC = gcc
# CFLAGS: Σημαίες μεταγλωττιστή
CFLAGS = -Wall

# LIBS: Βιβλιοθήκη για σύνδεση: Απαραίτητο το -lreadline
LIBS = -lreadline

# Ονόματα Αρχείων
TARGET = tinyshell
SOURCE = tinyshell.c

# 1. Κανόνας 'all' 
all: $(TARGET)

# 2. Κανόνας Δημιουργίας Εκτελέσιμου
# Στόχος: tinyshell
# Εξάρτηση: tinyshell.c
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET) $(LIBS)


