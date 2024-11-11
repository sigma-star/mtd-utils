#!/bin/sh
# Copyright (c), 2024, Huawei Technologies Co, Ltd.
# Author: Zhihao Cheng <chengzhihao1@huawei.com>
#
# Provide basic functions.

UBI_NUM=0
DEV=/dev/ubi0_0
MNT=/mnt/test_file_system
TMP_FILE=/tmp/ubifs_test_file
TMP_MNT=/tmp/ubifs_tmp_mnt
LOG_FILE=/tmp/ubifs_log
KEY_FILE=/tmp/key
IMG_FILE=ubifs.img
nandsim_patt="NAND simulator"
mtdram_patt="mtdram test device"

# fsck returning code
FSCK_OK=0		# No errors
FSCK_NONDESTRUCT=1	# File system errors corrected
FSCK_REBOOT=2		# System should be rebooted
FSCK_UNCORRECTED=4	# File system errors left uncorrected
FSCK_ERROR=8		# Operational error
FSCK_USAGE=16		# Usage or syntax error
FSCK_CANCELED=32	# Aborted with a signal or ^
FSCK_LIBRARY=128	# Shared library error

function fatal()
{
	echo "Error: $1" 1>&2
	exit 1
}

# All loaded modules(mtdram/nandsim/ubi/ubifs) won't be removed if errors
# happen, it is useful to debug based on the UBIFS image.
function cleanup_handler()
{
	local ret="$1"

	umount $TMP_MNT >/dev/null 2>&1 ||:
	rm -rf $TMP_MNT >/dev/null 2>&1 ||:
	if [ "$ret" == "0" ]; then
		umount $MNT >/dev/null 2>&1 ||:
		modprobe -r ubifs >/dev/null 2>&1 ||:
		modprobe -r ubi >/dev/null 2>&1 ||:
		modprobe -r nandsim >/dev/null 2>&1 ||:
		modprobe -r mtdram >/dev/null 2>&1  ||:
		rm -rf $MNT >/dev/null 2>&1 ||:
		rm -f $TMP_FILE >/dev/null 2>&1 ||:
		rm -f $KEY_FILE >/dev/null 2>&1 ||:
		rm -f $LOG_FILE >/dev/null 2>&1 ||:
		rm -f $IMG_FILE >/dev/null 2>&1 ||:
		exit 0
	else
		exit 1
	fi
}
trap 'cleanup_handler $?' EXIT
trap 'cleanup_handler 1' HUP PIPE INT QUIT TERM

function find_mtd_device()
{
	printf "%s" "$(grep "$1" /proc/mtd | sed -e "s/^mtd\([0-9]\+\):.*$/\1/")"
}

function powercut()
{
	dmesg -c > /dev/null
	echo 1 > /sys/kernel/debug/ubifs/tst_recovery;
	while true;
	do
		msg=`dmesg -c | grep "Power cut emulated"`;
		if [[ "$msg" != "" ]];
		then
			break;
		fi
		ro_error=`cat /sys/kernel/debug/ubifs/ubi${UBI_NUM}_0/ro_error`
		if [[ $ro_error != 0 ]]; then
			break;
		fi
	done
	echo 0 > /sys/kernel/debug/ubifs/tst_recovery
}

# Load mtdram with specified size and PEB size
# Usage: load_mtdram <flash size> <PEB size>
# 1. Flash size is specified in MiB
# 2. PEB size is specified in KiB
function load_mtdram()
{
	local size="$1";     shift
	local peb_size="$1"; shift

	size="$(($size * 1024))"
	modprobe mtdram total_size="$size" erase_size="$peb_size"
}

function check_fsstress()
{
	cmd=`fsstress | grep "op_name"`
	if ! [[ "$cmd" =~ "op_name" ]]; then
		fatal "fsstress is not found"
	fi
}

# Check error messages
function check_err_msg()
{
	msg=`dmesg | grep -E "dump_stack|UBIFS error|switched to read-only mode"`;
	if [[ "$msg" != "" ]]
	then
		dmesg
		fatal "error message detected!"
	fi
	dmesg -c > /dev/null
}

# Iterate all files under certain dir
# $1: dir
# $2: "md5sum" means that need record md5 for regular file, otherwise don't record md5 for regular file
function read_dir() {
	for file in `ls -a $1`
	do
		cur_f=$1"/"$file
		if [ -b $cur_f ]
		then
			major=`stat -c %t $cur_f`
			minor=`stat -c %T $cur_f`
			echo "block $cur_f $major $minor" >> $TMP_FILE
		elif [ -c $cur_f ]
		then
			major=`stat -c %t $cur_f`
			minor=`stat -c %T $cur_f`
			echo "char $cur_f $major $minor" >> $TMP_FILE
		elif [ -L $cur_f ]
		then
			link=`stat -c %N $cur_f`
			echo "symlink $cur_f $link" >> $TMP_FILE
		elif [ -S $cur_f ]
		then
			echo "sock $cur_f" >> $TMP_FILE
		elif [ -p $cur_f ]
		then
			echo "fifo $cur_f" >> $TMP_FILE
		elif [ -f $cur_f ]
		then
			sz=`stat -c %s $cur_f`
			if [[ "$2" != "md5sum" ]]; then
				echo "reg $cur_f $sz" >> $TMP_FILE
			else
				md5=`md5sum $cur_f | awk '{print $1}'`
				echo "reg $cur_f $md5 $sz" >> $TMP_FILE
			fi
		elif [ -d $cur_f ]
		then
			if [[ $file != '.' && $file != '..' ]]
			then
				echo "dir $cur_f" >> $TMP_FILE
				read_dir $1"/"$file $2
			fi
		else
			fatal "record unknown file type $cur_f"
		fi
	done
}

# Check whether there are files lost after fsck/mkfs
# $1: "md5sum" means need record md5 for regular file, otherwise don't check md5 for regular file
function parse_dir()
{
	while read line
	do
		array=(${line//\ / });
		f_type=${array[0]};
		cur_f=${array[1]};
		cur_info=""
		if [[ "$f_type" =~ "block" ]]
		then
			major=`stat -c %t $cur_f`
			minor=`stat -c %T $cur_f`
			cur_info="block $cur_f $major $minor"
		elif [[ "$f_type" =~ "char" ]]
		then
			major=`stat -c %t $cur_f`
			minor=`stat -c %T $cur_f`
			cur_info="char $cur_f $major $minor"
		elif [[ "$f_type" =~ "symlink" ]]
		then
			link=`stat -c %N $cur_f`
			cur_info="symlink $cur_f $link"
		elif [[ "$f_type" =~ "sock" ]]
		then
			cur_info="sock $cur_f"
		elif [[ "$f_type" =~ "fifo" ]]
		then
			cur_info="fifo $cur_f"
		elif [[ "$f_type" =~ "reg" ]]
		then
			sz=`stat -c %s $cur_f`
			if [[ "$1" != "md5sum" ]]; then
				cur_info="reg $cur_f $sz"
			else
				md5=`md5sum $cur_f | awk '{print $1}'`
				cur_info="reg $cur_f $md5 $sz"
			fi
		elif [[ "$f_type" =~ "dir" ]]
		then
			cur_info="dir $cur_f"
		else
			fatal "parse unknown file type $cur_f"
		fi
		if [[ "$cur_info" != "$line" ]]
		then
			fatal "current info $cur_info, but expect $line"
		fi
	done < $TMP_FILE
}

function authentication()
{
	keyctl clear @s
	res=$?
	if [[ $res != 0 ]]; then
		fatal "keyctl is not found"
	fi
	keyctl add logon ubifs:foo 12345678901234567890123456789012 @s
}

function encryption_gen_key()
{
	# CONFIG_FS_ENCRYPTION=y
	head -c 64 /dev/urandom > $KEY_FILE
	cmd=`fscryptctl -h | grep "set_policy"`
	if ! [[ "$cmd" =~ "set_policy" ]]; then
		fatal "fscryptctl is not found"
	fi
}

function encryption_set_key()
{
	mnt=$1
	ignore_err=$2
	# https://github.com/google/fscryptctl
	key=$(fscryptctl add_key $mnt < $KEY_FILE)
	fscryptctl set_policy $key $mnt
	#fscryptctl get_policy $mnt
	ret=$?
	if [[ $ret != 0 && $ignore_err != 1 ]]; then
		fatal "set encryption policy failed"
	fi
}

function mount_ubifs()
{
	local dev=$1;
	local mnt=$2;
	local auth=$3;
	local noatime=$4;
	local option="";
	if [[ "$noatime" == "noatime" ]]; then
		option="-o noatime"
	fi
	if [[ "$auth" == "authentication" ]]; then
		authentication
		if [[ "$option" == "" ]]; then
			option="-o auth_key=ubifs:foo,auth_hash_name=sha256"
		else
			option="$option,auth_key=ubifs:foo,auth_hash_name=sha256"
		fi
	fi
	mount -t ubifs $option $dev $mnt
}

function enable_chkfs()
{
	echo 1 > /sys/kernel/debug/ubifs/chk_fs
	echo 1 > /sys/kernel/debug/ubifs/chk_general
	echo 1 > /sys/kernel/debug/ubifs/chk_index
	echo 1 > /sys/kernel/debug/ubifs/chk_lprops
	echo 1 > /sys/kernel/debug/ubifs/chk_orphans
}

function disable_chkfs()
{
	echo 0 > /sys/kernel/debug/ubifs/chk_fs
	echo 0 > /sys/kernel/debug/ubifs/chk_general
	echo 0 > /sys/kernel/debug/ubifs/chk_index
	echo 0 > /sys/kernel/debug/ubifs/chk_lprops
	echo 0 > /sys/kernel/debug/ubifs/chk_orphans
}

function inject_mem_err()
{
	# CONFIG_FAILSLAB=y
	# CONFIG_FAIL_PAGE_ALLOC=y
	local pid=$1;

	if ! [ -f /sys/kernel/debug/failslab/probability ]; then
		fatal "failslab is not enabled, injection failed"
	fi
	if ! [ -f /sys/kernel/debug/fail_page_alloc/probability ]; then
		fatal "fail_page_alloc is not enabled, injection failed"
	fi

	echo 1 > /proc/$pid/make-it-fail

	echo Y > /sys/kernel/debug/failslab/task-filter
	echo 1 > /sys/kernel/debug/failslab/probability # 1% failure
	echo 10000 > /sys/kernel/debug/failslab/times
	echo 1 > /sys/kernel/debug/failslab/verbose
	echo N > /sys/kernel/debug/failslab/ignore-gfp-wait

	echo Y > /sys/kernel/debug/fail_page_alloc/task-filter
	echo 1 > /sys/kernel/debug/fail_page_alloc/probability
	echo 10000 > /sys/kernel/debug/fail_page_alloc/times
	echo 0 > /sys/kernel/debug/fail_page_alloc/verbose
	echo N > /sys/kernel/debug/fail_page_alloc/ignore-gfp-wait
}

function cancel_mem_err()
{
	echo 0 > /sys/kernel/debug/failslab/probability
	echo 0 > /sys/kernel/debug/failslab/times
	echo 0 > /sys/kernel/debug/failslab/verbose
	echo N > /sys/kernel/debug/failslab/task-filter
	echo Y > /sys/kernel/debug/failslab/ignore-gfp-wait

	echo 0 > /sys/kernel/debug/fail_page_alloc/probability
	echo 0 > /sys/kernel/debug/fail_page_alloc/times
	echo 1 > /sys/kernel/debug/fail_page_alloc/verbose
	echo N > /sys/kernel/debug/fail_page_alloc/task-filter
	echo Y > /sys/kernel/debug/fail_page_alloc/ignore-gfp-wait
}

function inject_io_err()
{
	if ! [ -f /sys/kernel/debug/ubi/ubi$UBI_NUM/tst_emulate_io_failures ]; then
		fatal "tst_emulate_io_failures is not enabled, skip injection"
	fi

	echo 1 > /sys/kernel/debug/ubi/ubi$UBI_NUM/tst_emulate_io_failures
}

function cancel_io_err()
{
	echo 0 > /sys/kernel/debug/ubi/ubi$UBI_NUM/tst_emulate_io_failures
}

if ! [ -d $MNT ]; then
	mkdir -p $MNT
fi
if ! [ -d $TMP_MNT ]; then
	mkdir -p $TMP_MNT
fi

modprobe ubi || fatal "common.sh: cannot load ubi"
modprobe ubifs || fatal "common.sh: cannot load ubifs"
modprobe -r ubifs
modprobe -r ubi
