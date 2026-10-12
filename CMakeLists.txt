# CMakeList.txt: PAK-server에 대한 CMake 프로젝트, 여기에 소스를 포함하고
# 프로젝트 특정 논리를 정의합니다.
#
cmake_minimum_required (VERSION 3.20)

# 지원되는 경우 MSVC 컴파일러에 대해 핫 다시 로드 사용하도록 설정합니다.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PAK-nativePlugin" LANGUAGES CXX)

# CRT MDd로
if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg toolchain이 연결되어 있다는 전제에서, Config package를 찾는다.
find_package(RtAudio CONFIG REQUIRED)
find_package(unofficial-asiosdk CONFIG REQUIRED)
find_package(Aubio CONFIG REQUIRED)
find_package(mpg123 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# NAM Core
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore")
file(GLOB NAM_CORE_SOURCES
  "${NAM_CORE_DIR}/NAM/*.cpp"
  "${NAM_CORE_DIR}/NAM/*.c"
  "${NAM_CORE_DIR}/NAM/*/*.cpp"
)

# Builds NAM DSP sources without adding its benchmark and rendering tool targets.
add_library(PAKNamCore STATIC ${NAM_CORE_SOURCES})
target_include_directories(PAKNamCore
  PUBLIC
    "${NAM_CORE_DIR}"
    "${NAM_CORE_DIR}/Dependencies/eigen"
    "${NAM_CORE_DIR}/Dependencies/nlohmann"
)
target_compile_features(PAKNamCore PUBLIC cxx_std_20)
target_compile_definitions(PAKNamCore PRIVATE
  NAM_ENABLE_A2_FAST
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable (duplex "test/soundTest/duplex.cpp")

target_link_libraries(duplex
  PRIVATE
    RtAudio::rtaudio
)

add_executable (duplexDSP "test/soundTest/duplexDSP.cpp")

target_link_libraries(duplexDSP
  PRIVATE
    RtAudio::rtaudio
)

# NAM 기타 입력 duplex 테스트
add_executable(NAMTest "test/performanceTest/NAMTest.cpp")
target_link_libraries(NAMTest
  PRIVATE
    RtAudio::rtaudio
    PAKNamCore
)
if (MSVC)
  target_link_options(NAMTest PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:PAKNamCore>")
endif()

# DLL
add_library(duplexPlugin SHARED "test/soundTest/duplexPlugin.cpp")

target_link_libraries(duplexPlugin
  PRIVATE
    RtAudio::rtaudio
)

# Unity Native Plugin DLL
add_library(PAKNativePlugin SHARED
  "src/audioQueue.cpp"
  "src/audioQueue.h"
  "src/eventQueue.cpp"
  "src/eventQueue.h"
  "src/main.cpp"
  "src/main.h"
  "src/setting.cpp"
  "src/setting.h"
  "src/plugin_state.h"
  "src/song.cpp"
  "src/song.h"
  "src/dsp.cpp"
  "src/dsp.h"
  "src/input.cpp"
  "src/input.h"
  "src/judge.cpp"
  "src/judge.h"
  "src/ChartParser.cpp"
)

if (WIN32)
  target_sources(PAKNativePlugin PRIVATE "src/PAKNativePlugin.rc")
endif()

target_link_libraries(PAKNativePlugin
  PRIVATE
    RtAudio::rtaudio
    unofficial::asiosdk::host
    Aubio::aubio
    MPG123::libmpg123
    nlohmann_json::nlohmann_json
    PAKNamCore
)

if (MSVC)
  # Keeps NAM's static config-parser registrations in the plugin DLL.
  target_link_options(PAKNativePlugin PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:PAKNamCore>")
endif()

# 설계 의도: DLL을 동적 로드하는 최소 실행 테스트를 별도 타겟으로 제공한다.
add_executable(duplexPluginTest "test/soundTest/duplexPluginTest.cpp")
add_dependencies(duplexPluginTest duplexPlugin)
add_custom_command(TARGET duplexPluginTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:duplexPlugin>"
          "$<TARGET_FILE_DIR:duplexPluginTest>"
)

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable (duplexPitch "test/soundTest/duplexPitch.cpp")

target_link_libraries(duplexPitch
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
)

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable (duplexChord "test/soundTest/duplexChord.cpp")

target_link_libraries(duplexChord
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
)

# 입력 시점 기준 Am 코드 판정 테스트
add_executable (chordTest "test/performanceTest/chordTest.cpp")

target_link_libraries(chordTest
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
)

# 수집 중에도 onset 분석을 유지하는 G5 코드 판정 비교 테스트
add_executable(chordOnsetTest "test/performanceTest/chordOnsetTest.cpp")

target_link_libraries(chordOnsetTest
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
)

# aubio onset 시각 기준으로 판정 창을 닫는 플러그인 창 비교 테스트
add_executable(judgeWindowTest "test/performanceTest/judgeWindowTest.cpp")
target_link_libraries(judgeWindowTest
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
)

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable (duplexTiming "test/soundTest/duplexTiming.cpp")

target_link_libraries(duplexTiming
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
)

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable (RhythmGame "test/play/RhythmGame.cpp" "test/feature/ChartParser.cpp")

target_link_libraries(RhythmGame
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
    nlohmann_json::nlohmann_json
)

# 이 프로젝트의 실행 파일에 소스를 추가합니다.
add_executable (RhythmGame_0.2v
  "test/play/RhythmGame_0.2v.cpp"
  "test/play/RhythmGame_0.2v.h"
  "test/feature/ChartParser.cpp"
)

target_link_libraries(RhythmGame_0.2v
  PRIVATE
    RtAudio::rtaudio
    Aubio::aubio
    nlohmann_json::nlohmann_json
)

# DLL 기반 리듬 게임 클라이언트 테스트
add_executable(RhythmGameDLL
  "test/play/RhythmGameDLL.cpp"
  "src/ChartParser.cpp"
)

target_link_libraries(RhythmGameDLL
  PRIVATE
    nlohmann_json::nlohmann_json
)
add_dependencies(RhythmGameDLL PAKNativePlugin)
target_compile_definitions(RhythmGameDLL
  PRIVATE
    PAK_DEFAULT_DLL_PATH="$<TARGET_FILE:PAKNativePlugin>"
)

# DLL 기반 천천히 재생 연습 속도 변경 테스트
add_executable(PracticeModeSlowDownTest
  "test/play/PracticeModeSlowDownTest.cpp"
)
add_dependencies(PracticeModeSlowDownTest PAKNativePlugin)

# DLL 기반 운지 연습 테스트
add_executable(PracticeModeFingeringTest
  "test/play/PracticeModeFingeringTest.cpp"
  "src/ChartParser.cpp"
)
target_link_libraries(PracticeModeFingeringTest
  PRIVATE
    nlohmann_json::nlohmann_json
)
add_dependencies(PracticeModeFingeringTest PAKNativePlugin)

# 기타 단독 기준 트랙으로 전체 채보 판정을 재생한다.
add_executable(JudgeTest
  "test/play/JudgeTest.cpp"
  "src/ChartParser.cpp"
)
target_link_libraries(JudgeTest
  PRIVATE
    MPG123::libmpg123
    nlohmann_json::nlohmann_json
)
target_compile_definitions(JudgeTest
  PRIVATE
    PAK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PAK_DEFAULT_DLL_PATH="$<TARGET_FILE:PAKNativePlugin>"
)
add_dependencies(JudgeTest PAKNativePlugin)

# 기타 단독 기준 트랙의 초반 36초만 재생한다.
add_executable(JudgeTest_36sec
  "test/play/JudgeTest_36sec.cpp"
  "src/ChartParser.cpp"
)
target_link_libraries(JudgeTest_36sec
  PRIVATE
    MPG123::libmpg123
    nlohmann_json::nlohmann_json
)
target_compile_definitions(JudgeTest_36sec
  PRIVATE
    PAK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PAK_DEFAULT_DLL_PATH="$<TARGET_FILE:PAKNativePlugin>"
)
add_dependencies(JudgeTest_36sec PAKNativePlugin)

# 녹음된 G5 입력으로 첫 24개 운지 연습 판정을 재현한다.
add_executable(G5fingeringTest
  "test/performanceTest/G5fingeringTest.cpp"
  "src/ChartParser.cpp"
)
target_link_libraries(G5fingeringTest
  PRIVATE
    MPG123::libmpg123
    nlohmann_json::nlohmann_json
)
target_compile_definitions(G5fingeringTest
  PRIVATE
    PAK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PAK_DEFAULT_DLL_PATH="$<TARGET_FILE:PAKNativePlugin>"
)
add_dependencies(G5fingeringTest PAKNativePlugin)

# DLL 기반 judge audio block 처리 속도 모니터링 테스트
add_executable(judgeConsumeTest
  "test/performanceTest/judgeConsumeTest.cpp"
)
add_dependencies(judgeConsumeTest PAKNativePlugin)

# DLL 기반 기타 입력 테스트
add_executable(guitarInputTest
  "test/feature/guitarInputTest.cpp"
)
add_dependencies(guitarInputTest PAKNativePlugin)

# DLL 기반 판정 이벤트 큐 포화 모니터링 테스트
add_executable(queueFullStateTest
  "test/performanceTest/queueFullStateTest.cpp"
)
add_dependencies(queueFullStateTest PAKNativePlugin)

# WASAPI 장치 연결 테스트
add_executable(driverConnectTest
  "test/performanceTest/driverConnectTest.cpp"
)
add_dependencies(driverConnectTest PAKNativePlugin)

# 출력 전용 테스트 톤 API 테스트
add_executable(audioTestApiTest
  "test/performanceTest/audioTestApiTest.cpp"
)
add_dependencies(audioTestApiTest PAKNativePlugin)

# ASIO SDK로 선택한 드라이버만 직접 초기화하는 연결 테스트
add_executable(asioDirectConnectTest
  "test/performanceTest/asioDirectConnectTest.cpp"
)
target_link_libraries(asioDirectConnectTest
  PRIVATE
    unofficial::asiosdk::host
)
target_compile_definitions(asioDirectConnectTest
  PRIVATE
    IEEE754_64FLOAT=1
)
