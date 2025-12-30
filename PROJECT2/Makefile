#
# Makefile
#
CC = arm-linux-gcc
LVGL_DIR_NAME = lvgl
LVGL_DIR = ${shell pwd}
CFLAGS = -O3 -g0 -I$(LVGL_DIR)/ -I$(LVGL_DIR)/lv_chinese_ime/ -I$(LVGL_DIR)/lv_chinese_ime/src -Wall -Wshadow -Wundef -Wmissing-prototypes -Wno-discarded-qualifiers -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wtype-limits -Wstack-usage=2048 -Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual -Wmissing-prototypes -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wno-discarded-qualifiers -Wformat-security -Wno-ignored-qualifiers -Wno-sign-compare
LDFLAGS ?= -lm
BIN = demo
BUILD_DIR = build

# 自动创建 build 目录（关键：确保目录存在）
$(shell mkdir -p $(BUILD_DIR))

#Collect the files to compile
MAINSRC = ./main.c

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

CSRCS +=$(LVGL_DIR)/mouse_cursor_icon.c  
CSRCS += $(LVGL_DIR)/lvgl/src/font/SiYuanHeiTi_Heavy.c  # 字体文件
CSRCS += $(shell find -L $(LVGL_DIR)/Squareline -name "*.c" -type f)
#CSRCS += $(shell find -L $(LVGL_DIR)/lv_chinese_ime -name "*.c" -type f)

OBJEXT ?= .o

# 关键：所有 .o 输出到 build/ 目录（统一路径）
AOBJS = $(addprefix $(BUILD_DIR)/, $(ASRCS:.S=$(OBJEXT)))
COBJS = $(addprefix $(BUILD_DIR)/, $(CSRCS:.c=$(OBJEXT)))
MAINOBJ = $(addprefix $(BUILD_DIR)/, $(MAINSRC:.c=$(OBJEXT)))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS)

all: default

# 关键：编译规则，指定 .o 输出到 build/，并自动创建子目录
$(BUILD_DIR)/%.o: %.c
# 自动创建 build/lvgl/src/font/ 等子目录
	@mkdir -p $(dir $@)  
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $< -> $(dir $@)"

# 链接：使用 build/ 下的所有 .o
default: $(AOBJS) $(COBJS) $(MAINOBJ) 
	$(CC) -o $(BIN) $^ $(LDFLAGS)
	@echo "LINK -> $(BIN)"

# 清理：删除 build/ 目录和可执行文件
clean: 
	rm -rf $(BUILD_DIR) $(BIN)
	@echo "Clean done"
