#!/bin/bash

set -e


CMAKE_VERSION=3.15.2
CMAKE=cmake-${CMAKE_VERSION}-Linux-x86_64
CMAKE_TAR=${CMAKE}.tar.gz
CMAKE_BIN=$PWD/${CMAKE}/bin/cmake

echo "$0 $@"
while getopts ":t:a:d:b:m:r:j" opt; do
  case $opt in
    t)
      TARGET_SOC=$OPTARG
      ;;
    a)
      TARGET_ARCH=$OPTARG
      ;;
    b)
      BUILD_TYPE=$OPTARG
      ;;
    m)
      ENABLE_ASAN=ON
      export ENABLE_ASAN=TRUE
      ;;
    d)
      BUILD_DEMO_NAME=$OPTARG
      ;;
    r)
      DISABLE_RGA=ON
      ;;
    j)
      DISABLE_LIBJPEG=ON
      ;;
    :)
      echo "Option -$OPTARG requires an argument." 
      exit 1
      ;;
    ?)
      echo "Invalid option: -$OPTARG index:$OPTIND"
      ;;
  esac
done

if [ -z ${TARGET_SOC} ] || [ -z ${BUILD_DEMO_NAME} ]; then
  echo "$0 -t <target> -a <arch> -d <build_demo_name> [-b <build_type>] [-m] [-r] [-j]"
  echo ""
  echo "    -t : target (rk356x/rk3588/rk3576/rv1126b/rv1106/rk1808/rv1126)"
  echo "    -a : arch (aarch64/armhf)"
  echo "    -d : demo name"
  echo "    -b : build_type(Debug/Release)"
  echo "    -m : enable address sanitizer, build_type need set to Debug"
  echo "    -r : disable rga, use cpu resize image"
  echo "    -j : disable libjpeg to avoid conflicts between libjpeg and opencv"
  echo "such as: $0 -t rk3588 -a aarch64 -d mobilenet"
  echo "Note: 'rk356x' represents rk3562/rk3566/rk3568, 'rv1106' represents rv1103/rv1106, 'rv1126' represents rv1109/rv1126, 'rv1126b' is different from 'rv1126'"
  echo "Note: 'disable rga option is invalid for rv1103/rv1103b/rv1106"
  echo "Note: 'if you want to use opencv to read or save jpg files, use the '-j' option to disable libjpeg"
  echo ""
  exit -1
fi

if echo ${TARGET_SOC} | grep -q "rv" ;then
    echo "Not support RV soc"
    exit
fi

if [ ${TARGET_ARCH} = "aarch64" ];then
    if [ ! -d "gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu" ];then
        if [ ! -f "gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz" ];then
            wget https://releases.linaro.org/components/toolchain/binaries/6.3-2017.05/aarch64-linux-gnu/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz
        fi
        if ! tar -tJf "gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz" > /dev/null 2>&1; then
            rm -f "gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz"
            echo "gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz is not a valid tar.xz file, exit"
            exit 1
        fi
        tar -xJf 'gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz'
        rm -f "gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu.tar.xz"
    fi
    GCC_COMPILER=$PWD/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu
elif [ ${TARGET_ARCH} = "armhf" ];then
    if [ ! -d "gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf" ];then
        if [ ! -f "gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz" ];then
            wget https://developer.arm.com/-/media/Files/downloads/gnu-a/8.3-2019.03/binrel/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz
        fi
        if ! tar -tJf "gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz" > /dev/null 2>&1; then
            rm -f "gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz"
            echo "gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz is not a valid tar.xz file, exit"
            exit 1
        fi
        tar -xJf 'gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz'
        rm -f "gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf.tar.xz"
    fi
    GCC_COMPILER=$PWD/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/bin/arm-linux-gnueabihf
fi

if [ ! -d ${CMAKE} ];then
	if [ ! -f ${CMAKE_TAR} ];then
		wget https://githubproxy.cc/https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_TAR}
	fi
  if ! tar -tzf "${CMAKE_TAR}" > /dev/null 2>&1; then
      rm -f "${CMAKE_TAR}"
      echo "${CMAKE_TAR} is not a valid tar.gz file, remove ${CMAKE_TAR}"
      exit 1
  fi
	if [ -f ${CMAKE_TAR} ];then
		tar -zxf ${CMAKE_TAR}
		rm ${CMAKE_TAR}
	fi
fi

if [[ -z ${GCC_COMPILER} ]];then
    if [[ ${TARGET_SOC} = "rv1106"  || ${TARGET_SOC} = "rv1103" ]];then
        echo "Please set GCC_COMPILER for $TARGET_SOC"
        echo "such as export GCC_COMPILER=~/opt/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf"
        exit
    elif [[ ${TARGET_SOC} = "rv1109" || ${TARGET_SOC} = "rv1126" ]];then
        GCC_COMPILER=arm-linux-gnueabihf
    else
        GCC_COMPILER=aarch64-linux-gnu
    fi
fi

export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

if command -v ${CC} >/dev/null 2>&1; then
    :
else
    echo "${CC} is not available"
    echo "Please set GCC_COMPILER for $TARGET_SOC"
    echo "such as export GCC_COMPILER=~/opt/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf"
    exit
fi

# Debug / Release
if [[ -z ${BUILD_TYPE} ]];then
    BUILD_TYPE=Release
fi

# Build with Address Sanitizer for memory check, BUILD_TYPE need set to Debug
if [[ -z ${ENABLE_ASAN} ]];then
    ENABLE_ASAN=OFF
fi

if [[ -z ${DISABLE_RGA} ]];then
    DISABLE_RGA=OFF
fi

if [[ -z ${DISABLE_LIBJPEG} ]];then
    DISABLE_LIBJPEG=OFF
fi

for demo_path in `find examples -name ${BUILD_DEMO_NAME}`
do
    if [ -d "$demo_path/cpp" ]
    then
        BUILD_DEMO_PATH="$demo_path/cpp"
        break;
    fi
done

if [[ -z "${BUILD_DEMO_PATH}" ]]
then
    echo "Cannot find demo: ${BUILD_DEMO_NAME}, only support:"

    for demo_path in `find examples -name cpp`
    do
        if [ -d "$demo_path" ]
        then
            dname=`dirname "$demo_path"`
            name=`basename $dname`
            echo "$name"
        fi
    done
    echo "rv1106_rv1103 only support: mobilenet and yolov5/6/7/8/x"
    exit
fi

case ${TARGET_SOC} in
    rk356x)
        ;;
    rk3588)
        ;;
    rv1106)
        ;;
    rv1103)
        TARGET_SOC="rv1106"
        ;;
    rk3566)
        TARGET_SOC="rk356x"
        ;;
    rk3568)
        TARGET_SOC="rk356x"
        ;;
    rk3562)
        TARGET_SOC="rk356x"
        ;;
    rk3576)
        TARGET_SOC="rk3576"
        ;;
    rk1808):
        TARGET_SOC="rk1808"
        ;;
    rv1109)
        ;;
    rv1126)
        TARGET_SOC="rv1126"
        ;;
    rv1126b)
        TARGET_SOC="rv1126b"
        ;;
    *)
        echo "Invalid target: ${TARGET_SOC}"
        echo "Valid target: rk3562,rk3566,rk3568,rk3588,rk3576,rv1106,rv1103,rk1808,rv1109,rv1126,rv1126b"
        exit -1
        ;;
esac

TARGET_SDK="rknn_${BUILD_DEMO_NAME}_demo"

TARGET_PLATFORM=${TARGET_SOC}_linux
if [[ -n ${TARGET_ARCH} ]];then
TARGET_PLATFORM=${TARGET_PLATFORM}_${TARGET_ARCH}
fi
ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )
INSTALL_DIR=${ROOT_PWD}/install/${TARGET_PLATFORM}/${TARGET_SDK}
BUILD_DIR=${ROOT_PWD}/build/build_${TARGET_SDK}_${TARGET_PLATFORM}_${BUILD_TYPE}

echo "==================================="
echo "BUILD_DEMO_NAME=${BUILD_DEMO_NAME}"
echo "BUILD_DEMO_PATH=${BUILD_DEMO_PATH}"
echo "TARGET_SOC=${TARGET_SOC}"
echo "TARGET_ARCH=${TARGET_ARCH}"
echo "BUILD_TYPE=${BUILD_TYPE}"
echo "ENABLE_ASAN=${ENABLE_ASAN}"
echo "DISABLE_RGA=${DISABLE_RGA}"
echo "DISABLE_LIBJPEG=${DISABLE_LIBJPEG}"
echo "INSTALL_DIR=${INSTALL_DIR}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "CC=${CC}"
echo "CXX=${CXX}"
echo "==================================="

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

if [[ -d "${INSTALL_DIR}" ]]; then
  rm -rf ${INSTALL_DIR}
fi

cd ${BUILD_DIR}
${CMAKE_BIN} ../../${BUILD_DEMO_PATH} \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=${TARGET_ARCH} \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DDISABLE_RGA=${DISABLE_RGA} \
    -DDISABLE_LIBJPEG=${DISABLE_LIBJPEG} \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
make -j4
make install

# Check if there is a rknn model in the install directory
suffix=".rknn"
shopt -s nullglob
if [ -d "$INSTALL_DIR" ]; then
    files=("$INSTALL_DIR/model/"/*"$suffix")
    shopt -u nullglob

    if [ ${#files[@]} -le 0 ]; then
        echo -e "\e[91mThe RKNN model can not be found in \"$INSTALL_DIR/model\", please check!\e[0m"
    fi
else
    echo -e "\e[91mInstall directory \"$INSTALL_DIR\" does not exist, please check!\e[0m"
fi
