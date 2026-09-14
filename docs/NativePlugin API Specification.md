# PAK Native Plugin API Specification

Unity에서 `PAKNativePlugin.dll`을 호출하기 위한 C API 명세서입니다.

## 기본 정보

- DLL 이름: `PAKNativePlugin`
- version: 0.5.2
- ABI: C ABI (`extern "C"`)
- Windows export: `__declspec(dllexport)`
- 오디오 샘플 포맷: signed 16-bit PCM
- 내부 버퍼 크기: 128 frames
- 세션 시작 전 카운트다운: 5000 ms
- 지원 채보 버전: `0.2.1`
- NAM 모델: `assets/NAM/VX TB30 BR Edge0 BAL2 CAB FREE.nam`

## 호출 순서

### 채보 판정 사용

```text
SetNamModelPath (선택)
  -> Initialize
      -> LoadChart
      -> ResetSessionTime
      -> SetDSPParams
      -> StartSession
          -> GetAudioStats 반복 호출
          -> PollJudgeEvent 반복 호출
      -> StopSession
      -> Shutdown
```

### 기타 입력만 사용

```text
Initialize
  -> SetDSPParams
  -> StartSession
      -> GetAudioStats 반복 호출
      -> PollGuitarInputEvent 반복 호출
  -> StopSession
  -> Shutdown
```

### 천천히 재생 연습

```text
Initialize
  -> LoadChart
  -> SetPracticeSpeed(0.25 ~ 1.25)
  -> StartSlowPracticeSession
      -> SetPracticeSpeed로 진행 중 속도 변경
      -> GetAudioStats / PollJudgeEvent 반복 호출
  -> StopSession
  -> Shutdown
```

### 기타 연결 테스트

```text
Initialize / InitializeWithAudioDevice / InitializeWithAsioDriver
  -> InitializeAudioTest
  -> StartAudioTest
      -> GetAudioTestOutputLevelDb 반복 호출
  -> StopAudioTest
  -> Shutdown
```

기타 입력을 출력 장치로 그대로 전달해 연결과 소리를 확인합니다. 이 경로에서는 공통 분석 스레드가
RMS만 계산하며 피치 분석, 곡 재생, 모니터 DSP를 시작하지 않습니다. 세션 API와 동시에 사용할 수 없습니다.

`StartSlowPracticeSession`은 채보 판정은 유지하고 곡 파일 대신 메트로놈을 출력에 섞습니다.
초기 속도는 100%이며, `SetPracticeSpeed`로 25%~125% 범위에서 즉시 바꿀 수 있습니다.

### 운지 연습

```text
Initialize
  -> LoadChart
  -> StartFingeringPracticeSession
      -> GetAudioStats / PollJudgeEvent 반복 호출
  -> StopSession
  -> Shutdown
```

현재 노트에서 채보 진행이 멈춥니다. 올바른 입력은 다음 노트로 진행하고, 틀린 입력은
`Miss` 이벤트만 발생시키며 현재 노트에 머뭅니다. 곡은 재생하지 않습니다.

### 녹음 기반 운지 테스트 (테스트 전용)

```text
InitializeFingeringTest
  -> LoadChart
  -> StartFingeringTestSession
      -> FeedFingeringTestAudio(128 frames) 반복 호출
      -> PollFingeringTestRawOnset / PollJudgeEvent 반복 호출
      -> GetJudgmentDiagnostics
  -> StopSession
  -> Shutdown
```

오디오 장치를 열지 않고, 녹음 PCM을 실제 운지 판정 queue와 judge thread 경로로 공급한다.
`G5fingeringTest` 같은 네이티브 회귀 테스트를 위한 API이며 Unity 런타임 사용을 위한 공개
워크플로는 아니다.

`Initialize`는 내부에서 기존 리소스를 먼저 정리한 뒤 새 오디오 스트림을 준비합니다.
사용자 NAM 모델을 사용할 때는 `Initialize` 전에 `SetNamModelPath`를 호출합니다.
`ResetSessionTime`은 새 세션 시작 전에 내부 시간과 판정 진행 상태를 0부터 다시 시작하도록 초기화합니다.
`LoadChart`는 채보 판정에만 필요하며, 기타 입력만 사용할 때는 호출하지 않아도 됩니다.
`StartSession`은 오디오 스트림과 판정 스레드를 시작하므로 `PollJudgeEvent`, `PollGuitarInputEvent`를 사용하기 전에 반드시 호출해야 합니다.
한 세션은 채보 판정 또는 기타 입력 중 하나의 모드로만 동작합니다.
`LoadChart`를 호출한 뒤 시작한 세션은 채보 판정 모드이며, `LoadChart` 없이 시작한 세션은 기타 입력 모드입니다.
세션 종료 시에는 `StopSession`을 호출하고, 플러그인을 더 이상 쓰지 않을 때 `Shutdown`을 호출합니다.

### NAM 모니터 DSP

`Initialize`, `InitializeWithAudioDevice`, `InitializeWithAsioDriver`로 오디오 스트림을
초기화하면 플러그인은 현재 선택된 NAM 모델을 한 번 로드합니다. 기본 경로는
`assets/NAM/VX TB30 BR Edge0 BAL2 CAB FREE.nam`이며, 다른 모델은 `Initialize` 전에
`SetNamModelPath`로 지정합니다. 모델 경로는 프로세스 작업 폴더 기준 상대 경로이므로,
배포 시에도 해당 경로에 모델 파일을 함께 배치해야 합니다.

판정 queue에는 NAM 처리 전의 원 기타 PCM이 들어갑니다. 따라서 NAM 앰프 시뮬레이션은 모니터
출력에만 영향을 주며, 피치·온셋·코드 판정에는 영향을 주지 않습니다.

NAM은 첫 번째 입력 채널을 mono 기타 입력으로 처리하고, 처리 결과를 모든 출력 채널에
사용합니다. 곡과 메트로놈은 NAM을 통과하지 않고 앰프 처리된 기타 신호에 섞입니다.

### 오디오 옵션 화면

옵션 화면은 WASAPI 장치와 ASIO 드라이버를 별도 경로로 조회합니다. 기본 연결은 Windows
기본 WASAPI 장치이며, ASIO 드라이버는 사용자가 명시적으로 선택할 때만 엽니다.

```text
# WASAPI 장치
GetAudioDeviceCount
  -> GetAudioDeviceInfo(index)
  -> InitializeWithAudioDevice(...)

# ASIO 드라이버
GetAsioDriverCount
  -> GetAsioDriverInfo(index)              // 등록 이름만 조회, 드라이버를 열지 않음
  -> SelectAsioDriver(name)                // 선택한 드라이버를 열고 채널 수 조회
  -> InitializeWithAsioDriver(name, ...)   // 해당 ASIO 드라이버로 스트림 초기화
```

`GetAudioDeviceInfo`의 `id`는 `InitializeWithAudioDevice`의 `inputDeviceId`와
`outputDeviceId`에 그대로 전달합니다. `inputChannels`는 해당 장치의 입력 채널 수이며,
UI에서는 1부터 `inputChannels`까지 표시합니다. 사용자가 고른 n번째 채널은
`channels = 1`, `inputOffset = n - 1`로 초기화합니다. RtAudio는 물리 입력의 별도 이름을
제공하지 않으므로 채널 표시는 `Input 1`, `Input 2`처럼 순번으로 구성합니다.

ASIO 목록에는 `GetAudioDeviceCount`와 `GetAudioDeviceInfo`를 사용하면 안 됩니다.
두 함수는 WASAPI 장치만 조회합니다.
`GetAsioDriverInfo`는 Windows 레지스트리의 등록 이름만 읽습니다.
`SelectAsioDriver`는 선택한 드라이버만 `ASIOInit`으로 열어 채널 수를 반환하고, 다음
드라이버 선택 또는 `Shutdown`까지 열린 상태로 유지합니다.
`InitializeWithAsioDriver`는 이 선택 상태를 닫은 뒤 RtAudio ASIO 백엔드로 같은 이름의
드라이버를 찾아 실제 duplex 스트림을 엽니다. 입력과 출력은 해당 드라이버의 채널 offset을
사용합니다.

곡 음량은 `SetSongVolume`으로 설정합니다. 이는 기타 입력의 모니터 게인과 별개이며,
기존 `SetDSPParams`의 `inputGain`과 `outputGain`은 그대로 유지됩니다.

## Unity C# 선언 예시

```csharp
using System.Runtime.InteropServices;

public static class PakNativePlugin
{
    private const string DllName = "PAKNativePlugin";

    public enum JudgeResult
    {
        Perfect = 0,
        Good = 1,
        Bad = 2,
        Miss = 3,
        Wrong = 4,
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct JudgeEvent
    {
        public int noteIndex;
        public int result;
        public double judgedAudioTimeMs;
        public double judgedChartTimeMs;
        public float errorMs;
        public int detectedMidi;
        public int targetMidi;
        public int stringNumber;
        public int fret;
        public int startMs;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string noteName;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GuitarInputEvent
    {
        public int midi;
        public double audioTimeMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FingeringTestRawOnset
    {
        public double audioTimeMs;
        public int startedJudgment;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JudgmentDiagnostics
    {
        public uint detectedOnsets;
        public uint startedFingeringJudgments;
        public uint passedChordJudgments;
        public uint failedChordJudgments;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JudgeProcessingStats
    {
        public uint queuedAudioBlocks;
        public uint processedAudioBlocks;
        public uint droppedAudioBlocks;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct AudioStats
    {
        public double streamTime;
        public double audioTimeMs;
        public double chartTimeMs;
        public double countdownMs;
        public int streamLatency;
        public uint bufferFrames;
        public uint droppedAudioBlocks;
        public uint droppedJudgeEvents;
        public int totalNotes;
        public int nextNoteIndex;
        public int isRunning;
        public int isFinished;
        public int isPaused;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct SongSyncInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string audioFile;
        public int audioOffsetMs;
        public int durationMs;
        public double songTimeMs;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct AsioDriverInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string name;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct AudioDeviceInfo
    {
        public uint id;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string name;

        public uint inputChannels;
        public uint outputChannels;
        public uint duplexChannels;
        public int isDefaultInput;
        public int isDefaultOutput;
        public uint preferredSampleRate;
    }

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern string GetPluginVersion();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int Initialize(
        uint channels,
        uint sampleRate,
        uint inputDevice,
        uint outputDevice,
        uint inputOffset,
        uint outputOffset);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern void SetNamModelPath(
        [MarshalAs(UnmanagedType.LPStr)] string modelPath);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint GetAsioDriverCount();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetAsioDriverInfo(uint driverIndex, out AsioDriverInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int SelectAsioDriver(
        [MarshalAs(UnmanagedType.LPStr)] string driverName,
        out AudioDeviceInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int InitializeWithAsioDriver(
        [MarshalAs(UnmanagedType.LPStr)] string driverName,
        uint channels,
        uint sampleRate,
        uint inputOffset,
        uint outputOffset);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint GetAudioDeviceCount();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetAudioDeviceInfo(uint deviceIndex, out AudioDeviceInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int InitializeWithAudioDevice(
        uint channels,
        uint sampleRate,
        uint inputDeviceId,
        uint outputDeviceId,
        uint inputOffset,
        uint outputOffset);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int InitializeAudioTest();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int StartAudioTest();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void StopAudioTest();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetAudioTestOutputLevelDb(out float outLevelDb);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int LoadChart(
        [MarshalAs(UnmanagedType.LPStr)] string chartPath);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void ResetSessionTime();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int StartSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int StartSlowPracticeSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetPracticeSpeed(float speed);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int StartFingeringPracticeSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int InitializeFingeringTest(
        uint channels,
        uint sampleRate,
        [MarshalAs(UnmanagedType.LPStr)] string onsetMethod,
        float onsetThreshold);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int StartFingeringTestSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FeedFingeringTestAudio(short[] samples, uint frames);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int PollFingeringTestRawOnset(out FingeringTestRawOnset outOnset);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void StopSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void PauseSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void ResumeSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int RestartSession();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetDSPParams(float inputGain, float outputGain, float lpfAlpha);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetSongVolume(float volume);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SetGuitarInputIntervalMs(double intervalMs);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int PollJudgeEvent(out JudgeEvent outEvent);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int PollGuitarInputEvent(out GuitarInputEvent outEvent);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetAudioStats(out AudioStats outStats);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetJudgmentDiagnostics(out JudgmentDiagnostics outDiagnostics);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetJudgeProcessingStats(out JudgeProcessingStats outStats);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetSongSyncInfo(out SongSyncInfo outInfo);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Shutdown();
}
```

## JudgeResult

| 값  | 이름      | 의미                                      |
| --- | --------- | ----------------------------------------- |
| 0   | `Perfect` | 타이밍 오차가 60 ms 이하이고 피치가 일치  |
| 1   | `Good`    | 타이밍 오차가 140 ms 이하이고 피치가 일치 |
| 2   | `Bad`     | 타이밍 오차가 240 ms 이하이고 피치가 일치 |
| 3   | `Miss`    | 타이밍 범위를 벗어났거나 입력이 없음      |
| 4   | `Wrong`   | 타이밍 범위 안이지만 피치가 불일치        |

`single` 이벤트의 피치 판정은 목표 MIDI와 감지 MIDI가 정확히 같아야 통과합니다.
`chord` 이벤트는 아래의 CG-FPM 코드 판정 결과가 일치해야 통과합니다.

## 채보 interpretation과 코드 판정

`schemaVersion: "0.2.1"`의 각 `notes` 이벤트는 `interpretation`으로 판정 방식을 지정합니다.

| `interpretation` | 채보 필수 정보                          | 판정 방식            | 플러그인 이벤트 수 |
| ---------------- | --------------------------------------- | -------------------- | ------------------ |
| `single`         | `string`, `fret`, `finger`, `technique` | aubio MIDI 피치 판정 | 노트당 1개         |
| `chord`          | `chordId`, `strumTechnique`             | CG-FPM 코드 판정     | 코드 이벤트당 1개  |

`single` 이벤트의 `technique`에는 `normal`, `hammer_on`, `pull_off`, `slide`, `bend`, `palm_mute`, `cutting`을 사용합니다. `0.2.1`부터 `dead_note`는 제거되고 `cutting`이 추가되었습니다.

`chordId`는 `track.chordDefinitions`의 `id`를 참조합니다. 플러그인은 해당 코드의 `fingering`과 `track.tuning`으로 실제 MIDI 음 목록을 계산합니다. 뮤트(`fret: -1`) 줄은 코드 판정 대상에서 제외합니다.

코드는 운지된 줄마다 단음 이벤트로 분리되지 않습니다. 하나의 chord 이벤트가 하나의 판정 대상이며, `noteName`에는 코드 기호(`chordDefinitions[].symbol`)가 담깁니다.

### CG-FPM: Chart-Guided Fundamental Presence Matching

CG-FPM은 채보 운지의 각 MIDI 기본음 대역에 에너지가 있는지 확인하는 방식입니다. 코드 전체를 후보 중에서 분류하지 않으며, 한 음의 배음이 다른 목표음의 근거가 되지 않도록 기본음 대역만 사용합니다.

1. 코드 onset 기준 160 ms 모노 입력 window를 수집합니다. 운지 판정에서는 raw onset마다
   독립 pending judgment와 PCM window를 만들므로, 기존 chord judgment가 수집 중이어도
   후속 onset이 버려지지 않습니다.
2. Hann window와 16,384-point FFT를 적용합니다.
3. 채보 운지 MIDI 각각의 기본음 주파수 주변 5개 FFT bin 에너지를 계산합니다.
4. 각 목표 기본음 주변에서 최강 스펙트럼 피크가 해당 목표 기본음 FFT bin에 있는지 확인합니다.
5. 모든 목표음 에너지가 목표음 중 최강 기본음 에너지의 15% 이상이면 일치로 판정합니다.

예를 들어 G5(`G2`, `D3`)는 약 98 Hz와 147 Hz 대역이 모두 있어야 통과합니다. 또한 각 대역 주변의 최강 피크가 목표 주파수에 있어야 하므로, 인접한 F#5나 G#5처럼 다른 주파수가 더 강한 입력은 거절합니다. G2의 3차 배음인 약 294 Hz는 D3의 기본음 대역이 아니므로 D3의 근거로 사용되지 않습니다.

### CG-HCM: Chart-Guided Harmonic Chroma Matching (보존됨, 현재 미사용)

CG-HCM은 채보가 요구하는 코드가 입력 스펙트럼에 포함됐는지 확인하는 방식입니다. 코드 자체를 전체 후보 중에서 새로 분류하지 않습니다.

1. 코드 onset 뒤 160 ms 동안 모노 입력 샘플을 수집합니다.
2. Hann window와 16,384-point FFT를 적용합니다.
3. 채보 운지 MIDI와 인접 옥타브에서 기본음 및 최대 6개 배음의 에너지를 계산합니다.
4. 에너지를 12개 pitch class chroma로 합산합니다.
5. 목표 코드톤 chroma 에너지 비율이 0.65 이상이고, 모든 목표 pitch class가 최강 pitch class 에너지의 15% 이상이면 일치로 판정합니다.

160 ms 수집 구간은 스트럼에서 줄마다 소리가 나는 시점 차이를 반영합니다. 따라서 다음 코드 onset이 160 ms 이내에 들어오면 두 스트럼의 스펙트럼이 겹쳐 판정이 흔들릴 수 있습니다.

현재 방식은 일반 스트럼과 컷팅에서 실험 중입니다. 팜뮤트는 배음과 지속 시간이 줄어들어 인식률이 낮을 수 있으며, 빠른 연속 스트럼 및 코드 교체도 별도 실험이 필요합니다.

## JudgeEvent

`PollJudgeEvent`로 가져오는 노트 1개의 판정 결과입니다.

| 필드                | 타입       | 의미                                                                         |
| ------------------- | ---------- | ---------------------------------------------------------------------------- |
| `noteIndex`         | `int`      | 채보의 노트 인덱스. 0부터 시작                                               |
| `result`            | `int`      | `JudgeResult` 값                                                             |
| `judgedAudioTimeMs` | `double`   | 플러그인 오디오 스트림 기준 판정 시각                                        |
| `judgedChartTimeMs` | `double`   | 채보 기준 판정 시각                                                          |
| `errorMs`           | `float`    | `judgedChartTimeMs - startMs`                                                |
| `detectedMidi`      | `int`      | `single`: 감지 MIDI. 감지되지 않으면 0 / `chord`: 0 (사용 안 함)             |
| `targetMidi`        | `int`      | `single`: 목표 MIDI / `chord`: 0 (사용 안 함)                                |
| `stringNumber`      | `int`      | `single`: 기타 줄 번호 / `chord`: 0 (사용 안 함)                             |
| `fret`              | `int`      | `single`: 프렛 번호 / `chord`: 0 (사용 안 함)                                |
| `startMs`           | `int`      | 채보 노트 시작 시각                                                          |
| `noteName`          | `char[16]` | `single`: 목표 음 이름. 예: `E2`, `F#3` / `chord`: 코드 기호. 예: `C`, `Am7` |

`chord` 이벤트에 대해 `detectedMidi`와 `targetMidi`를 비교하면 안 됩니다. 두 필드는 0이므로, 예를 들어 `MIDI 48/0`은 코드의 목표 MIDI가 0이라는 뜻이지 입력이 없었다는 뜻은 아닙니다.

chord window는 detector가 반환한 onset 시각을 기준으로 잡는다. 따라서 `judgedAudioTimeMs`는
HFC peak-picking 결과이며, 사용자가 느끼는 스트로크 시작 시각과 정확히 같다고 가정하면 안 된다.
window anchoring 변경은 chord 판정 의미를 바꾸므로 별도 검증이 필요하다.

## GuitarInputEvent

`PollGuitarInputEvent`로 가져오는 기타 입력 이벤트입니다.

채보 판정과 별개로, 입력 오디오에서 onset이 감지된 뒤 80 ms 피치 안정 구간을 기다리고 MIDI 피치가 확인되면 이벤트가 발생합니다.
MIDI 피치가 감지되지 않으면 이벤트를 발생시키지 않습니다.

| 필드          | 타입     | 의미                                   |
| ------------- | -------- | -------------------------------------- |
| `midi`        | `int`    | 감지된 MIDI 피치                       |
| `audioTimeMs` | `double` | 플러그인 오디오 스트림 기준 onset 시각 |

## AudioStats

`GetAudioStats`로 가져오는 현재 세션 상태입니다.

| 필드                 | 타입     | 의미                                                                                        |
| -------------------- | -------- | ------------------------------------------------------------------------------------------- |
| `streamTime`         | `double` | 세션 기준 스트림 시간. 초 단위                                                              |
| `audioTimeMs`        | `double` | 오디오 스트림 시간. ms 단위                                                                 |
| `chartTimeMs`        | `double` | 채보 시간. 천천히 재생 연습에서는 설정한 속도로 진행, 운지 연습에서는 현재 노트 시각에 고정 |
| `countdownMs`        | `double` | 세션 시작 전 카운트다운 시간. 현재 5000                                                     |
| `streamLatency`      | `int`    | RtAudio 스트림 지연                                                                         |
| `bufferFrames`       | `uint`   | 내부 오디오 버퍼 프레임 수. 현재 128                                                        |
| `droppedAudioBlocks` | `uint`   | 판정 스레드로 전달하지 못한 오디오 블록 수                                                  |
| `droppedJudgeEvents` | `uint`   | Unity가 늦게 polling해서 버려진 판정 이벤트 수                                              |
| `totalNotes`         | `int`    | 로드된 채보의 전체 노트 수                                                                  |
| `nextNoteIndex`      | `int`    | 다음 판정 대상 노트 인덱스                                                                  |
| `isRunning`          | `int`    | 오디오 스트림 실행 중이면 1, 아니면 0                                                       |
| `isFinished`         | `int`    | 모든 노트 판정이 끝났으면 1, 아니면 0                                                       |
| `isPaused`           | `int`    | 세션이 일시정지 상태이면 1, 아니면 0                                                        |

`chartTimeMs`가 0보다 작으면 카운트다운 구간입니다.

## Functions

### GetPluginVersion

```c
const char *GetPluginVersion(void);
```

로드된 네이티브 플러그인의 버전 문자열을 반환합니다.
초기화 전후와 관계없이 호출할 수 있습니다.

현재 반환값은 `"0.5.2"`입니다.

### Initialize

```c
int Initialize(
    unsigned int channels,
    unsigned int sampleRate,
    unsigned int inputDevice,
    unsigned int outputDevice,
    unsigned int inputOffset,
    unsigned int outputOffset);
```

오디오 스트림, NAM 모니터 모델, DSP 상태, 판정 큐, aubio 피치/온셋 감지기를 초기화합니다.

| 파라미터       | 의미                                 |
| -------------- | ------------------------------------ |
| `channels`     | 입출력 채널 수                       |
| `sampleRate`   | 샘플레이트. 예: 48000                |
| `inputDevice`  | 입력 장치 선택. 0이면 기본 입력 장치 |
| `outputDevice` | 출력 장치 선택. 0이면 기본 출력 장치 |
| `inputOffset`  | 입력 장치의 시작 채널                |
| `outputOffset` | 출력 장치의 시작 채널                |

반환값:

- `0`: 성공
- `-1`: 오디오 스트림 생성 실패

### InitializeAudioTest

```c
int InitializeAudioTest(void);
```

현재 `Initialize`, `InitializeWithAudioDevice`, `InitializeWithAsioDriver`로 열어 둔 스트림을
기타 연결 테스트 모드로 준비합니다. 스트림의 드라이버와 장치 설정은 바꾸지 않습니다.

반환값:

- `0`: 성공
- `-1`: 열린 스트림이 없거나 스트림이 실행 중임

### StartAudioTest

```c
int StartAudioTest(void);
```

`InitializeAudioTest`로 준비한 현재 스트림을 시작합니다. 기타를 연주하면 해당 입력이
현재 연결된 출력 장치로 재생됩니다.

반환값:

- `0`: 성공
- `-1`: 오디오 테스트가 초기화되지 않았거나 스트림 시작 실패

### StopAudioTest

```c
void StopAudioTest(void);
```

실행 중인 기타 연결 테스트 스트림을 정지합니다.

### GetAudioTestOutputLevelDb

```c
int GetAudioTestOutputLevelDb(float *outLevelDb);
```

분석 스레드가 계산한 최근 입력 버퍼의 RMS 레벨을 dBFS로 복사합니다. 기타를 치지 않아 입력이 무음이면
`-96 dBFS`이며, 기타 입력이 들어오면 값이 커집니다. 이 값은 실제 스피커 음압(dB SPL)이 아닌
디지털 PCM 신호 레벨입니다.

반환값:

- `0`: 성공
- `-1`: 오디오 테스트가 초기화되지 않음

### LoadChart

```c
int LoadChart(const char *chartPath);
```

다음 세션에서 사용할 채보 JSON 파일을 로드합니다.
기타 입력만 사용할 때는 호출하지 않아도 됩니다.
채보의 `song.audioFile` MP3도 함께 PCM으로 디코딩합니다.

반환값:

- `0`: 성공
- `-1`: 채보 파일 로드 실패

채보는 `schemaVersion`, `song`, `track`, `notes` 구조를 사용합니다.
노트의 `startTick`은 BPM과 resolution을 기준으로 ms로 변환됩니다.

`0.2.1` chord 이벤트 예시:

```json
{
  "interpretation": "chord",
  "startTick": 480,
  "durationTick": 480,
  "chordId": "C_open",
  "strumTechnique": "down"
}
```

### ResetSessionTime

```c
void ResetSessionTime(void);
```

내부 세션 시각, 다음 판정 노트 인덱스, 판정 큐, 종료 플래그를 초기화합니다.

새 게임을 시작하기 전 호출하면 `GetAudioStats`의 `audioTimeMs`, `chartTimeMs`가 이전 세션 시간에 영향을 받지 않습니다.
`StartSession`도 내부에서 이 함수를 한 번 호출합니다.

### StartSession

```c
int StartSession(void);
```

오디오 스트림과 판정 스레드를 시작합니다.

반환값:

- `0`: 성공
- `-1`: 초기화되지 않았거나 스트림 시작 실패

세션 시작 직후 5000 ms 카운트다운이 적용됩니다.

### StartSlowPracticeSession

```c
int StartSlowPracticeSession(void);
```

곡 대신 준비된 메트로놈 효과음을 재생하는 천천히 재생 연습 세션을 시작합니다. `LoadChart`를
먼저 호출해야 하며, 채보의 BPM과 `audioOffsetMs`에 맞춰 4분음표마다 클릭이 재생됩니다.
채보와 메트로놈, 카운트다운은 현재 연습 속도로 진행됩니다.

### SetPracticeSpeed

```c
void SetPracticeSpeed(float speed);
```

천천히 재생 연습의 채보 진행 속도를 설정합니다. 허용 범위는 `0.25f`~`1.25f`이며,
범위를 벗어난 값은 가장 가까운 허용값으로 적용됩니다. 실행 중 호출하면 다음 오디오 버퍼부터 반영됩니다.

### StartFingeringPracticeSession

```c
int StartFingeringPracticeSession(void);
```

곡을 재생하지 않고 현재 노트의 채보 시각에서 진행을 멈춥니다. 올바른 피치 또는 코드 입력은
`Perfect` 이벤트를 발생시키고 다음 노트로 이동합니다. 틀린 입력은 `Miss` 이벤트를 발생시키며
현재 노트에 남습니다. 채보의 시간 오차는 판정에 사용하지 않습니다.

### InitializeFingeringTest

```c
int InitializeFingeringTest(
    unsigned int channels,
    unsigned int sampleRate,
    const char *onsetMethod,
    float onsetThreshold);
```

장치를 열지 않는 녹음 기반 운지 테스트 경로를 초기화합니다. `onsetMethod`는 aubio onset
descriptor 이름이며, `onsetThreshold`는 해당 detector의 threshold입니다. 현재 기준 테스트
설정은 adaptive whitening OFF입니다.

반환값:

- `0`: 성공
- `-1`: 초기화 실패

### StartFingeringTestSession

```c
int StartFingeringTestSession(void);
```

녹음 PCM을 받을 운지 판정 judge thread를 시작합니다. `InitializeFingeringTest`와 `LoadChart`
뒤에 호출합니다.

### FeedFingeringTestAudio

```c
int FeedFingeringTestAudio(const int16_t *samples, unsigned int frames);
```

녹음 PCM block 하나를 정상 운지 판정 audio queue에 넣습니다. `frames`는 내부 buffer 크기와 같은
128이어야 합니다.

반환값:

- `0`: queue에 입력 성공
- `-1`: 테스트 세션이 아니거나 frame 수가 다름, 또는 queue가 가득 참

### StopSession

```c
void StopSession(void);
```

현재 오디오 스트림을 멈추고 판정 스레드를 종료합니다.

### PauseSession

```c
void PauseSession(void);
```

현재 세션의 오디오 출력과 입력 판정을 멈춥니다. 세션과 채보 시각은 정지한 상태로 유지됩니다.

### ResumeSession

```c
void ResumeSession(void);
```

`PauseSession`으로 멈춘 세션을 기존 진행도에서 재개합니다.

### RestartSession

```c
int RestartSession(void);
```

현재 세션을 처음부터 다시 시작합니다. 실행 중이던 세션 모드(일반 게임, 느린 연습, 운지 연습)를 유지하며,
세션 시각과 판정 진행도를 초기화합니다.

반환값:

- `0`: 성공
- `-1`: 초기화되지 않았거나 스트림 시작 실패

### SetDSPParams

```c
void SetDSPParams(float inputGain, float outputGain, float lpfAlpha);
```

모니터링 오디오의 DSP 파라미터를 갱신합니다.

처리 순서:

```text
guitar input (channel 0) -> input gain -> NAM amp model
chart song / metronome mix -> one-pole low-pass filter -> output gain
```

| 파라미터     | 의미                  | 기본값 |
| ------------ | --------------------- | ------ |
| `inputGain`  | NAM 모델 앞 입력 게인 | 4.0    |
| `outputGain` | 출력 게인             | 0.5    |
| `lpfAlpha`   | low-pass filter alpha | 0.2    |

### SetNamModelPath

```c
void SetNamModelPath(const char *modelPath);
```

다음 `Initialize`, `InitializeWithAudioDevice`, `InitializeWithAsioDriver` 호출에서 로드할
`.nam` 모델 파일 경로를 설정합니다. 반드시 해당 초기화 API 전에 호출합니다. 호출하지 않으면
기본 경로 `assets/NAM/VX TB30 BR Edge0 BAL2 CAB FREE.nam`를 사용합니다.

```csharp
PakNativePlugin.SetNamModelPath("assets/NAM/my-amp.nam");
PakNativePlugin.Initialize(channels, sampleRate, inputDevice, outputDevice, inputOffset, outputOffset);
```

### SetGuitarInputIntervalMs

```c
void SetGuitarInputIntervalMs(double intervalMs);
```

기타 입력 이벤트 사이의 최소 간격을 설정합니다. 기본값은 `150.0`ms이며, 이 시간 안에
발생한 후속 입력은 Unity로 전달하지 않습니다. `0.0`을 설정하면 제한하지 않습니다.

### PollFingeringTestRawOnset

```c
int PollFingeringTestRawOnset(FingeringTestRawOnset *outOnset);
```

녹음 기반 운지 테스트에서 검출된 raw onset 하나를 가져옵니다. `startedJudgment`는 해당 raw
onset으로 judgment를 시작했으면 `1`, 시작하지 못했으면 `0`입니다. overlap 구조에서는 chart에
남은 타깃이 있는 raw onset마다 `1`이 됩니다.

반환값:

- `1`: onset 있음
- `0`: 대기 중 onset 없음

| 필드              | 타입     | 의미                                               |
| ----------------- | -------- | -------------------------------------------------- |
| `audioTimeMs`     | `double` | aubio가 보고한 plugin audio stream 기준 onset 시각 |
| `startedJudgment` | `int`    | judgment 생성 여부. 0 또는 1                       |

### PollJudgeEvent

```c
int PollJudgeEvent(JudgeEvent *outEvent);
```

대기 중인 판정 이벤트를 하나 가져옵니다.

반환값:

- `1`: 이벤트 있음. `outEvent`에 값이 복사됨
- `0`: 이벤트 없음

Unity에서는 한 프레임 안에서 `0`이 나올 때까지 반복 호출하면 됩니다.

```csharp
while (PakNativePlugin.PollJudgeEvent(out var judgeEvent) == 1)
{
    // judgeEvent 처리
}
```

### PollGuitarInputEvent

```c
int PollGuitarInputEvent(GuitarInputEvent *outEvent);
```

대기 중인 기타 입력 이벤트를 하나 가져옵니다.

반환값:

- `1`: 이벤트 있음. `outEvent`에 값이 복사됨
- `0`: 이벤트 없음

채보 판정 이벤트와 독립적으로 동작하므로 Client 조작용 입력에 사용할 수 있습니다.
기타 입력 모드는 채보를 로드하지 않은 상태에서 `StartSession`을 호출해 시작합니다.
채보 판정 모드로 시작한 세션에서는 `PollGuitarInputEvent`가 이벤트를 반환하지 않습니다.
Unity에서는 MIDI 값을 원하는 동작에 매핑하면 됩니다.

```csharp
while (PakNativePlugin.PollGuitarInputEvent(out var inputEvent) == 1)
{
    if (inputEvent.midi == 40) // E2
    {
        // 확인 처리
    }
}
```

### GetAudioStats

```c
int GetAudioStats(AudioStats *outStats);
```

현재 오디오 시간, 채보 진행도, drop count, 종료 여부를 가져옵니다.

반환값:

- `0`: 성공
- `-1`: 초기화되지 않음

### GetJudgmentDiagnostics

```c
int GetJudgmentDiagnostics(JudgmentDiagnostics *outDiagnostics);
```

운지 연습의 onset 전달과 chord 판정 수를 가져옵니다. raw onset detector 문제와 judgment
구조 또는 chord 판정 문제를 분리하는 진단용 API입니다.

| 필드                        | 타입   | 의미                                           |
| --------------------------- | ------ | ---------------------------------------------- |
| `detectedOnsets`            | `uint` | raw onset 검출 수                              |
| `startedFingeringJudgments` | `uint` | raw onset에서 시작한 운지 judgment 수          |
| `passedChordJudgments`      | `uint` | G5 fundamental 판정을 통과한 chord judgment 수 |
| `failedChordJudgments`      | `uint` | G5 fundamental 판정에 실패한 chord judgment 수 |

반환값:

- `0`: 성공
- `-1`: 초기화되지 않음

### GetJudgeProcessingStats

```c
int GetJudgeProcessingStats(JudgeProcessingStats *outStats);
```

장치 callback-to-judge audio queue의 처리 수를 가져옵니다. 실시간 입력에서 queue drop을
점검할 때 사용합니다.

| 필드                   | 타입   | 의미                                 |
| ---------------------- | ------ | ------------------------------------ |
| `queuedAudioBlocks`    | `uint` | queue에 넣은 audio block 수          |
| `processedAudioBlocks` | `uint` | judge thread가 처리한 audio block 수 |
| `droppedAudioBlocks`   | `uint` | queue가 가득 차 버린 audio block 수  |

반환값:

- `0`: 성공
- `-1`: 장치 기반 오디오 세션이 초기화되지 않음

### GetSongSyncInfo

```c
int GetSongSyncInfo(SongSyncInfo *outInfo);
```

로드된 채보의 곡 파일과 네이티브 오디오 시계에 동기화된 재생 시각을 가져옵니다.
곡은 `LoadChart`에서 DLL 내부 PCM 버퍼로 읽히며, `StartSession` 뒤 카운트다운이 끝나면
기타 입력과 믹싱되어 DLL의 RtAudio 출력과 DSP를 통과합니다. `StartSlowPracticeSession`에서는
곡 대신 메트로놈을 믹싱합니다.

| 필드            | 타입        | 의미                                                     |
| --------------- | ----------- | -------------------------------------------------------- |
| `audioFile`     | `char[260]` | 채보 `song.audioFile` 경로                               |
| `audioOffsetMs` | `int`       | 채보 tick 변환에 사용된 `song.audioOffsetMs`             |
| `durationMs`    | `int`       | 채보에 기록된 곡 길이                                    |
| `songTimeMs`    | `double`    | 곡 시작 기준 재생 시각. 천천히 재생 연습에서는 채보 시각 |

`songTimeMs`는 DLL이 실제로 출력하는 곡의 재생 위치입니다. 카운트다운 중에는 음수이며,
0 이상부터 곡 PCM이 RtAudio 출력에 믹싱됩니다.

반환값:

- `0`: 성공
- `-1`: 초기화되지 않았거나 채보가 로드되지 않음

### Shutdown

```c
void Shutdown(void);
```

오디오 스트림, 판정 스레드, aubio 리소스를 해제합니다.

## Unity 채보 판정 루프 예시

```csharp
void Update()
{
    if (PakNativePlugin.GetAudioStats(out var stats) != 0)
        return;

    if (stats.chartTimeMs < 0)
    {
        // countdown UI
    }

    while (PakNativePlugin.PollJudgeEvent(out var judgeEvent) == 1)
    {
        var result = (PakNativePlugin.JudgeResult)judgeEvent.result;
        // 판정 UI / 점수 처리
    }

    if (stats.isFinished == 1)
    {
        // 결과 화면
    }
}
```

## Unity 곡 재생 시각 조회 예시

```csharp
void Update()
{
    if (PakNativePlugin.GetSongSyncInfo(out var song) != 0)
        return;

    // song.songTimeMs는 DLL이 출력 중인 곡 위치다.
    songProgressSlider.value = (float)(song.songTimeMs / song.durationMs);
}
```

Unity `AudioSource`로 같은 곡을 별도로 재생하면 이중 출력되므로 사용하지 않습니다.

## Unity 기타 입력 루프 예시

```csharp
void Update()
{
    if (PakNativePlugin.GetAudioStats(out var stats) != 0)
        return;

    while (PakNativePlugin.PollGuitarInputEvent(out var inputEvent) == 1)
    {
        if (inputEvent.midi == 40)
        {
            // E2 입력 처리
        }
    }
}
```
