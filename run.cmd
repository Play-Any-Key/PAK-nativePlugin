@echo off
pushd "%~dp0"
out\build\ninja-release\RhythmGameDLL.exe 2 48000 %*
popd
:: ./duplex 1 48000 <입력장치번호> <출력장치번호> <입력오프셋> <출력오프셋>
:: 입력장치 = 오디오 인터페이스
:: 오프셋 = 오디오 인터페이스에 몇번 포트에 연결된건지
