#!/bin/bash

set -e

ANDROID_NDK=android-ndk-r18b
ANDROID_NDK_ZIP=${ANDROID_NDK}-linux-x86_64.zip
ANDROID_NDK_PATH=$PWD/${ANDROID_NDK}

CMAKE_VERSION=3.15.2
CMAKE=cmake-${CMAKE_VERSION}-Linux-x86_64
CMAKE_TAR=${CMAKE}.tar.gz
CMAKE_BIN=$PWD/${CMAKE}/bin/cmake

rm -rf $PWD/build

if [ ! -d ${ANDROID_NDK_PATH} ];then
	if [ ! -f ${ANDROID_NDK_ZIP} ];then
		wget https://dl.google.com/android/repository/${ANDROID_NDK_ZIP}
	fi
  if ! unzip -t "${ANDROID_NDK_ZIP}" > /dev/null 2>&1; then
      rm -f "${ANDROID_NDK_ZIP}"
      echo "${ANDROID_NDK_ZIP} is not a valid zip file, remove ${ANDROID_NDK_ZIP}"
      exit 1
  fi
	if [ -f $PWD/${ANDROID_NDK_ZIP} ];then
		unzip $PWD/${ANDROID_NDK_ZIP}
		rm $PWD/${ANDROID_NDK_ZIP}
	fi
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


if [[ -z ${ANDROID_NDK_PATH} ]];then
    echo "Please set ANDROID_NDK_PATH, such as ANDROID_NDK_PATH=~/opts/ndk/android-ndk-r18b"
    echo "NDK Version r18, r19 is recommanded. Other version may cause build failure."
    exit
fi

if [[ -e ${ANDROID_NDK_PATH}/source.properties ]];then
    ndk_version=`strings ${ANDROID_NDK_PATH}/source.properties |  grep -oE 'Revision = ([0-9]+)' | awk '{print $NF}'`
    # echo "NDK Version ${ndk_version}"
    if [ "$ndk_version" != "18" ] && [ "$ndk_version" != "19" ] && [ "$ndk_version" != "" ]; then
      #`"$ndk_version" != ""` used to avoid build script reporting error when it cannot read ndk_version
      echo "NDK Version ${ndk_version} is not recommanded. Please use 18 or 19"
      exit
    fi
fi

echo "$0 $@"
while getopts ":t:a:b:m:r:j" opt; do
  case $opt in
    t)
      TARGET_SOC=$OPTARG
      ;;
    a)
      TARGET_ARCH=$OPTARG
      ;;
    # d)
    #   BUILD_DEMO_NAME=$OPTARG
    #   ;;
    b)
      BUILD_TYPE=$OPTARG
      ;;
    m)
      ENABLE_ASAN=ON
      export ENABLE_ASAN=TRUE
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

if [ -z ${TARGET_SOC} ]  || [ -z ${TARGET_ARCH} ]; then
  echo "$0 -t <target> -a <arch> -d <build_demo_name> [-b <build_type>] [-m] [-r] [-j]"
  echo ""
  echo "    -t : target (rk356x/rk3588/rk3576)"
  echo "    -a : arch (arm64-v8a/armeabi-v7a)"
  # echo "    -d : demo name"
  echo "    -b : build_type (Debug/Release)"
  echo "    -m : enable address sanitizer, build_type need set to Debug"
  echo "    -r : disable rga, use cpu resize image"
  echo "    -j : disable libjpeg to avoid conflicts between libjpeg and opencv"
  echo "such as: $0  -t rk3588 -a arm64-v8a -d yolov5"
  echo "Note: 'rk356x' represents rk3562/rk3566/rk3568"
  echo "Note: 'if you want to use opencv to read or save jpg files, use the '-j' option to disable libjpeg"
  echo ""
  exit -1
fi

# Debug / Release / RelWithDebInfo
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

case ${TARGET_SOC} in
    rk356x)
        ;;
    rk3588)
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
    *)
        echo "Invalid target: ${TARGET_SOC}"
        echo "Valid target: rk3562,rk3566,rk3568,rk3588,rk3576"
        exit -1
        ;;
esac

for prj_itm in $PWD/src/*; do
    echo "=====> ${prj_itm}"
    if [ -d "$prj_itm/cpp" ]; then

        BUILD_DEMO_NAME=`basename ${prj_itm}`
    
        BUILD_DEMO_PATH="${prj_itm}/cpp"
        TARGET_SDK="rknn_${BUILD_DEMO_NAME}_demo"

        TARGET_PLATFORM=""
        TARGET_PLATFORM=${TARGET_SOC}_android
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
        echo "ANDROID_NDK_PATH=${ANDROID_NDK_PATH}"
        echo "==================================="

        if [[ ! -d "${BUILD_DIR}" ]]; then
          mkdir -p ${BUILD_DIR}
        fi

        if [[ -d "${INSTALL_DIR}" ]]; then
          rm -rf ${INSTALL_DIR}
        fi

        cd ${BUILD_DIR}
        ${CMAKE_BIN} ${BUILD_DEMO_PATH} \
                -DTARGET_SOC=${TARGET_SOC} \
                -DANDROID_PLATFORM=android-23 \
                -DCMAKE_SYSTEM_NAME=Android \
                -DCMAKE_SYSTEM_VERSION=23 \
                -DCMAKE_ANDROID_ARCH_ABI=${TARGET_ARCH} \
                -DCMAKE_ANDROID_STL_TYPE=c++_static \
                -DCMAKE_ANDROID_NDK=${ANDROID_NDK_PATH} \
                -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                -DENABLE_ASAN=${ENABLE_ASAN} \
                -DDISABLE_RGA=${DISABLE_RGA} \
                -DDISABLE_LIBJPEG=${DISABLE_LIBJPEG} \
                -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
        # make VERBOSE=1
        make -j4
        make install
        cd -
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

    fi
done
