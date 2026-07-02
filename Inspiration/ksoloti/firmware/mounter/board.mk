# List of all the board related files.
BOARDSRC = $(CHIBIOS)/os/hal/boards/Brick/board.c

# Required include directories
BOARDINC = $(CHIBIOS)/os/hal/boards/Brick

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
