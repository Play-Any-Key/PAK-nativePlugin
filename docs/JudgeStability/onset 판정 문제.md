# G5 연속 스트로크 onset 판정 실험

## 최종 결론

이번 실험에서 onset detector 자체는 현재 목적에 충분한 수준까지 도달했다. 이후의 최우선
과제는 detector를 더 늘리는 일이 아니라, detector가 반환한 onset 시각을 chord 판정 window에
어떻게 연결할지 결정하는 일이다.

- **adaptive spectral whitening은 폐기한다.** 실제 ON 상태에서 threshold를 `0.04`부터
  `0.30`까지 올려도 whitening OFF + `0.04`의 raw F1 93.9%에 도달하지 못했다. ON 내부의
  최고 F1도 threshold `0.10`에서 51.3%다.
- **SpecFlux/Complex 비교는 보류한다.** 현재 HFC detector는 필요한 onset 후보를 충분히
  제공하므로, 추가 descriptor 비교보다 window anchoring을 먼저 검증한다.
- **overlapping judgment 구조는 유지한다.** 160ms settle을 줄이지 않고 `5985.9ms`와 같은
  raw TP의 pending 유실을 제거한다. 이후 chord fail은 detector 문제가 아니라 chord
  window/판정 문제로 분리된다.
- **다음 병목은 onset 시간 위치와 chord window anchoring이다.** aubio/HFC가 보고하는
  onset 시각은 연주자가 의도한 strike 시작, 고주파 peak, chord FFT에 적합한 window 시작점과
  서로 다를 수 있다.

다음 실험에서는 HFC, whitening OFF, threshold `0.04`, overlapping judgment와 160ms settle을
고정한다. 사용자 아이디어에 따른 anchoring 방식만 바꾸고, raw onset 시각·실제 window 구간·
chord pass/fail을 함께 기록한다.

## 목적과 입력

운지 연습 모드에서 연속으로 들어오는 G5 파워코드 스트로크가 첫 24개 타깃까지
안정적으로 진행되는지 확인한다.

- `G5`는 실제 G5 음역이 아니라 파워코드 이름이다.
- 현재 G5 운지는 6번 줄 3프렛(G2), 5번 줄 5프렛(D3)으로 낮은 음역이다.
- 실제 연주에서는 타깃 두 줄 외 네 줄의 커팅 소리가 함께 들어올 수 있다.

## 테스트 구성

- 입력 파일: `assets/testSound/G5stroke*.mp3`
- 차트: `assets/charts/PAK - Night.json`의 처음 24개 G5 코드 타깃
- 테스트 실행 파일: `test/performanceTest/G5fingeringTest.cpp`
- 입력 방식: MP3를 128-frame 단위로 공급해 기존 audio queue와 judge thread 경로를 사용한다.
- 결과 지표
  - `detectedOnsets`: `aubio_onset_do()`가 검출한 onset 수
  - `startedFingeringJudgments`: 실제로 타깃 판정을 시작한 수
  - `chord pass/fail`: 시작한 판정의 코드 통과/실패 수
  - `targets correct`: Target 24까지 순서대로 진행한 수

이 테스트는 장치 지연은 포함하지 않지만, callback-to-judge queue와 onset/chord 판정 경로는
실제 플러그인과 같다.

## 관찰한 구조

현재 onset 검출과 운지 판정은 다음 순서로 동작한다.

```text
aubio_onset_do()
  ↓
detectedOnsets 증가
  ↓
raw onset마다 새 타깃 판정 시작 (pending judgment와 겹침 허용)
  ↓
각 judgment가 160ms(CHORD_SETTLE_MS) PCM window를 독립 수집
  ↓
fundamental 판정 성공 시 다음 타깃으로 진행
```

`new_aubio_onset("default", ...)`의 `default` descriptor는 aubio에서 HFC(High
Frequency Content)다. 초기 기준선의 peak-picking threshold는 `0.2f`였고, 현재 후보
조건은 **adaptive whitening OFF와 threshold `0.04f`**다.

## 초기 기준선과 참고 기록

| 입력 / 설정                            | 타깃 통과 | detected | started | chord pass / fail | 해석                                                  |
| -------------------------------------- | --------: | -------: | ------: | ----------------: | ----------------------------------------------------- |
| `G5stroke.mp3`                         |     14/24 |       17 |      17 |            14 / 3 | 기본 녹음에서 15번 이후로 진행하지 못함               |
| `G5stroke_gainUp.mp3`                  |     15/24 |       18 |      18 |            15 / 3 | 입력 게인만 올려서는 큰 개선 없음                     |
| gain-up + fundamental peak ±1 bin      |     16/24 |       18 |      18 |            16 / 2 | 코드 판정 1건 개선, onset 수는 동일                   |
| `G5stroke_cuttingRemove.mp3` + ±1 bin  |      9/24 |       14 |      13 |             9 / 4 | 커팅을 제거하면 onset이 더 감소                       |
| gain-up + HFC(default) + threshold 0.2 |     16/24 |       19 |      18 |            16 / 2 | threshold 하향은 대기 중 중복 onset 1개만 추가        |
| gain-up + energy + threshold 0.2       |     11/24 |       25 |      14 |            11 / 3 | raw onset은 증가했지만 과검출과 대기 구간 손실로 악화 |

### 판정 대기 중 onset 보존 실험

`pendingJudgments`가 존재하는 동안에도 onset 후보와 이후 160ms 샘플을 보존하고,
이전 타깃 판정이 끝난 뒤 순서대로 연결하는 구현을 한 번 적용했다.

결과는 `16/24`, `detected 19`, `started 19`, `chord pass/fail 16/3`이었다.

- 이전에는 `detected 19 / started 18`이었고, 후보 보존으로 1개를 더 판정했다.
- 추가 처리된 onset은 `1961.5ms`였으며, `88.5ms` 뒤 `2050.0ms`에 실제 Target 2
  통과 onset이 다시 들어왔다.
- 즉 추가 후보는 다음 스트로크가 아니라 하나의 스트로크에서 나온 중복 onset이었다.
- 타깃 진행 수는 늘지 않았고 chord fail만 1개 증가했다.

따라서 당시의 순차 후보 보존 변경은 되돌렸다. 이는 현재의 독립 PCM window를 사용하는
judgment overlap 구현과는 다른 방식이다.

### 반복성

gain-up + HFC(default) + threshold 0.2를 3회 실행했을 때 결과와 onset 시각이 모두
동일했다.

```text
16/24 targets correct
detected 19 / started 18 / chord pass 16 / chord fail 2
```

테스트 결과는 재생 타이밍, queue, judge thread의 우연한 변동이 아니라 입력 신호와
현재 detector 설정에 의해 결정된다.

### HFC onset 시각에서 보인 공백

대표 실행에서 다음 공백이 반복됐다.

```text
4200.9ms → 4922.0ms : 721.1ms
5628.1ms → 6150.3ms : 522.2ms
7591.7ms 이후       : 후속 onset 없음
```

일반적인 검출 간격은 약 160~190ms 또는 346~373ms였다. 따라서 위 구간은 실제
스트로크가 detector에서 누락됐을 가능성이 높은 구간이다. 단, 녹음에 별도 정답 onset
마커가 없으므로 정확한 누락 스트로크 개수는 아직 확정하지 않는다.

## 확인된 판단

### 1. raw onset과 judgment 손실을 분리해야 한다

raw onset 시각을 CSV와 직접 매칭해 whitening 적용 여부를 다시 검증했다. 이전에 이 문서에서
`whitening ON + 0.04`로 기록한 좋은 결과는 실제로 whitening 호출이 빠진 **OFF** 실행의
결과였다. 따라서 기존 ON 결론은 무효다.

```text
whitening OFF + threshold 0.04
  raw TP 23 / FP 2 / FN 1 (FN: 7207ms up weak)

whitening ON + threshold 0.04
  raw TP 13 / FP 19 / FN 11
```

whitening ON은 TP를 10개 줄이고 FP와 FN을 동시에 크게 늘렸다. 이 입력에서는 whitening을
사용하지 않는다. 성능 개선에 유의미했던 변경은 HFC threshold를 `0.2`에서 `0.04`로 낮춘
것이다.

- **detector 문제**: whitening OFF 기준 `7207ms up weak`은 raw onset 자체가 없다.
- **구조 문제**: `5985.9ms` raw TP는 overlap 구현에서 started judgment로 전달되지만,
  chord fundamental 판정은 별도로 실패할 수 있다.

raw onset과 chord 판정을 분리해야 detector 문제를 구조 또는 chord 문제로 잘못 해석하지 않는다.

### 2. 커팅은 제거할 노이즈가 아니다

커팅을 천으로 제거한 녹음은 onset이 18회에서 14회로 감소했다. 커팅과 피킹의
attack/transient는 낮은 G2/D3 파워코드에서 HFC onset을 만드는 유용한 단서다.

따라서 목표는 커팅을 제거하는 것이 아니라, 자연스러운 커팅을 포함한 연주에서 onset을
안정적으로 검출하는 것이다.

### 3. energy 단독 방식은 현재 입력에서 적합하지 않다

energy는 HFC보다 많은 raw onset(25회)을 검출했지만, 한 스트로크 안의 에너지 변화도
여러 번 잡아 과검출했다. 160ms 판정 대기와 결합되면서 실제 시작 판정 수는 14회로
감소했고, 최종 결과도 11/24로 나빠졌다.

현재 입력에서는 HFC가 energy보다 좋은 기준선이다.

### 4. 판정 대기 중 후보 보존과 판단 중첩은 구분한다

이전 후보 보존 실험에서 추가 처리된 `1961.5ms` onset은 현재 `x,x` 라벨로 분류된
의도하지 않은 소리였다. 따라서 이를 다음 target으로 순차 연결하는 방식은 적합하지 않다.

반면 `5963ms` raw TP는 유효한 스트로크다. 다음 해결책은 기존 160ms settle을 줄이는 것이
아니라, 새 raw onset마다 별도 160ms chord sample window를 열어 **판단을 겹쳐 수행**하는
방식이다.

## 세부 검증 기록

### 1. HFC + adaptive spectral whitening (완료: 채택하지 않음)

`InitializeFingeringTest()`에서 onset detector 생성 직후 아래 설정을 적용했다.

```cpp
aubio_onset_set_awhitening(g_state.onsetDetector, 1);
```

처음 진행한 threshold sweep은 `InitializeFingeringTest()`에 whitening 호출이 없던 상태에서
수행됐다. 따라서 그 표의 `whitening ON` 행은 실제로 OFF 측정이며, ON 성능 근거로 사용할 수
없다. 호출을 복구한 뒤 `G5stroke_gainUp.mp3`와 같은 라벨로 다시 측정했다. 아래 raw 지표는
detector만 비교하므로 judgment overlap과 독립적이다.

| 실제 설정                     | raw TP | raw FP | raw FN | Recall | Precision |        F1 |
| ----------------------------- | -----: | -----: | -----: | -----: | --------: | --------: |
| whitening OFF, threshold 0.04 |     23 |      2 |      1 |  95.8% |     92.0% | **93.9%** |
| whitening ON, threshold 0.04  |     13 |     19 |     11 |  54.2% |     40.6% |     46.4% |
| whitening ON, threshold 0.05  |     12 |     18 |     12 |  50.0% |     40.0% |     44.4% |
| whitening ON, threshold 0.10  |     10 |      5 |     14 |  41.7% |     66.7% | **51.3%** |
| whitening ON, threshold 0.15  |      6 |      0 |     18 |  25.0% |    100.0% |     40.0% |
| whitening ON, threshold 0.20  |      4 |      0 |     20 |  16.7% |    100.0% |     28.6% |
| whitening ON, threshold 0.30  |      2 |      0 |     22 |   8.3% |    100.0% |     15.4% |

threshold를 올리면 FP는 감소하지만 TP가 더 크게 줄어든다. ON 내부의 최고 F1은 `0.10`의
51.3%지만, OFF `0.04`의 93.9%에 크게 못 미치며 타깃 통과도 11/24로 하락했다. 그러므로
현재 입력에서 adaptive whitening은 threshold 조절로도 채택할 수 없는 옵션이다.

현재 후보는 **whitening OFF + threshold 0.04**다. whitening을 다시 검토하려면 ON 상태를
명시적으로 보장한 별도 sweep과 다른 녹음 세트가 필요하다.

### 2. chord settle 길이 비교 (완료: 160ms 유지)

`5963ms` 주변 라벨 간격이 빽빽한 구간에서 chord sample 수집 시간이 onset 손실 또는
코드 판정 실패에 미치는 영향을 확인했다. `G5stroke_gainUp.mp3`를 128-frame 단위로
공급하는 기존 `G5fingeringTest` 경로를 사용하고, HFC(`default`)와 threshold `0.20`을
고정했다. 각 조건은 독립 Debug DLL로 빌드했다.

| chord settle | onset TP / FP / FN | 타깃 통과 | chord pass / fail |
| -----------: | -----------------: | --------: | ----------------: |
|         80ms |         17 / 0 / 7 |      8/24 |            8 / 11 |
|        100ms |         16 / 0 / 8 |     13/24 |            13 / 5 |
|        120ms |         17 / 0 / 7 |     15/24 |            15 / 3 |
|    **160ms** |     **17 / 0 / 7** | **16/24** |        **16 / 2** |

onset은 CSV 라벨과 비교할 수 있어 TP/FP/FN으로 표기했다. chord는 onset으로 시작된
판정마다 G5 fundamental 통과 여부만 기록하므로, 별도 음성 chord 정답 라벨이 없는 현재
테스트에서는 FP를 정의할 수 없다. 따라서 chord 지표는 pass/fail을 유지한다.

- 80ms에서는 `1961.5ms`의 중복 onset까지 새 판정으로 시작한다. 그러나 chord sample
  수집량이 부족해 fail이 11건으로 증가했다.
- 100ms는 `1961.5ms` 중복 onset이 다음 `2050.0ms` onset을 선점해, `2030ms` down strong
  라벨까지 놓쳤다.
- 120ms는 160ms에 근접하지만 chord pass가 1건 적다.
- 당시에는 started judgment만 비교했기 때문에 `5963ms` down strong을 detector FN으로
  기록했다. raw onset 매칭 결과 `5985.9ms` raw TP가 존재하며, 기존 pending judgment가
  있어 버려진 구조적 손실임이 확인됐다.

따라서 이 입력에서는 **CHORD_SETTLE_MS를 160ms로 유지**한다. 더 짧은 수집 창은 다음
onset을 더 빨리 받을 수 있어도 chord fundamental 판정 품질 저하가 더 크다. `5963ms` 개선은
settle 단축이 아니라, settle window를 유지한 병렬 judgment 처리에서 다룬다.

### 3. 의도하지 않은 초반 소리 제거 입력 (완료)

`G5test_removeUnexpectSound.mp3`는 원본의 `1861.9ms`, `1961.6ms` 의도하지 않은 소리를
삭제한 파일이다. 라벨에서도 두 `x,x` 항목을 제거하고, 의도한 스트로크 24개와 마지막
`7895ms x,x` 항목만 유지했다. 아래는 whitening 적용 여부가 검증되기 전 병렬 judgment
실험 결과이므로 whitening ON 성능 근거로는 사용하지 않는다.

2회 실행 결과가 동일했다.

| 입력           | raw / started | raw TP / FP / FN | judgment TP / FP / FN | pending dropped | chord pass / fail |
| -------------- | ------------: | ---------------: | --------------------: | --------------: | ----------------: |
| 원본 gain-up   |       25 / 25 |       23 / 0 / 1 |            23 / 0 / 1 |               0 |            21 / 4 |
| 초반 소리 제거 |       23 / 23 |       23 / 0 / 1 |            23 / 0 / 1 |               0 |            19 / 4 |

- 삭제한 두 소리는 새 파일에서 raw onset으로 검출되지 않았다.
- 의도한 스트로크의 raw TP와 judgment TP는 모두 23개로 유지됐다.
- 유일한 detector FN은 계속 `7207ms up weak`이다.
- `5963ms down strong`은 새 파일에서도 raw TP와 judgment TP이므로, pending 구조적 유실은
  제거된 상태다.

다만 첫 의도 스트로크는 원본과 다르게 chord fail이 됐다.

```text
원본: 2050.3ms → 2030ms down strong, Correct
편집 파일: 2021.4ms → 2030ms down strong, Incorrect
```

편집 파일의 첫 onset 시각은 라벨보다 8.6ms 빠르고, 2회 모두 같은 chord fail이 발생했다.
따라서 삭제 과정의 편집 경계가 첫 스트로크 주변 waveform 또는 160ms chord FFT window에
영향을 준 것으로 판단한다. 이 파일은 onset 구조 검증에는 유효하지만, chord pass/fail을
자연스러운 원본과 동등하게 비교하는 입력으로는 사용하지 않는다.

### 4. settle 유지 + judgment 겹침 (완료: onset 유실 제거)

`CHORD_SETTLE_MS = 160ms`는 유지한다. raw onset이 들어오면 기존 pending judgment가 있어도
새 target용 pending judgment를 만들고, 각 judgment가 독립적으로 이후 160ms sample을
수집하도록 한다.

- `5985.9ms` raw TP는 더 이상 버려지지 않고 `5963ms` down strong의 chord 판정까지
  수행된다.
- raw onset과 started judgment가 모두 25개(원본), 23개(초반 소리 제거 파일)가 되어 pending
  onset 유실이 제거됐다.
- `5963ms`의 chord 결과는 fail이므로, 해결된 것은 onset 전달 구조이며 chord fundamental
  판정 품질은 별도 문제로 남아 있다.

### 5. 7207ms weak-up ODF 상태 확인 (완료)

`G5fingeringTest`에 test 전용 ODF 전달 경로를 추가했다. `aubio_onset_do()` 직후 각 hop의
`audioTimeMs`, descriptor, thresholded descriptor, threshold, `hasOnset`을 기록하고,
`docs/판정 안정성/G5stroke_odf.csv`로 저장한다. 이 기록은 no-device recorded-input test에서만
활성화된다.

#### 실행 조건과 재현 결과

adaptive whitening ON, HFC(`default`), threshold `0.04`, 1024-sample buffer, 128-sample hop으로
`G5stroke_gainUp.mp3`를 실행했다. CSV의 모든 threshold 값은 `0.040`이었다.

```text
16/24 targets correct
detected / started = 33 / 33
raw TP / FP / FN = 13 / 19 / 11
chord pass / fail = 25 / 8
```

병렬 judgment에서는 raw onset마다 새 judgment를 시작하므로 `started`가 `detected`와 같다.
따라서 33은 judgment 전달 문제나 pending drop이 아니라 whitening ON + `0.04`의 raw
과검출 결과다. 이 값은 whitening OFF + `0.04` 결과보다 나쁘며, whitening ON 조합을
후보로 채택하지 않는 위 결론과 일치한다.

#### 7207ms와 7322ms onset 해석

| 기준 구간                         | thresholded ODF peak |     peak 시각 | hasOnset   |
| --------------------------------- | -------------------: | ------------: | ---------- |
| `5791ms` up weak (검출 성공)      |              128.584 |    5787.574ms | 5790.476ms |
| `6129ms` up weak                  |              -15.159 |    6089.433ms | 없음       |
| `7207ms` up weak 직후 7160~7250ms |            음수 유지 | 최대 -164.583 | 없음       |

`7207ms` 뒤 `7328.798ms`에는 thresholded ODF peak `153.509`가, `7331.701ms` hop에는
`hasOnset`이 기록됐다. aubio가 보고한 raw onset 시각은 약 `7322ms`다.

수동 라벨은 고주파 peak가 아니라 스트로크 **시작**을 기준으로 찍었다. 특히 `7207ms up weak`은
24개 중 시작과 peak가 가장 멀며, 수동 확인 peak는 `7267ms`다. 따라서 `7322ms` raw onset은
다음과 같이 두 라벨 사이에 있다.

| 비교 기준                             | 7322ms와의 차이 |
| ------------------------------------- | --------------: |
| `7207ms` up weak 시작                 |          +115ms |
| `7207ms` up weak의 수동 peak `7267ms` |           +55ms |
| 다음 `7398ms down strong` 시작        |           -76ms |

현재 고정 매칭 허용 범위 ±75ms에서는 `7322ms → 7398ms`도 약 1ms 밖이므로 매칭되지 않는다.
그러나 라벨 기준과 detector 기준이 다르므로, 이 수치만으로 `7322ms`를 다음 down strong으로
분류하거나 `7207ms` 라벨 오류로 결론낼 수 없다. 7207ms가 시작→peak 간격이 긴 예외 스트로크인
점을 고려하면, 7322ms는 weak-up의 늦은 HFC/aubio 반응일 가능성이 있다.

다음 확인은 `7207ms`, `7267ms`, `7322ms`, `7398ms` 주변 파형/스펙트로그램을 함께 보고,
다음 down strong의 실제 고주파 peak 시각을 결정하는 것이다. 그 peak가 7322ms 부근이면
다음 down strong의 이른 검출이고, 그렇지 않으면 7207ms weak-up의 늦은 검출이다.

#### 실행 메모

Windows CMD에서는 UTF-8 한글 경로를 인자로 넘기면 좁은 문자열 경로가 깨져 `abort()`가 발생할
수 있었다. `run.cmd`는 빌드 디렉터리로 이동한 뒤 인자 없이 기본 설정을 실행하도록 정리했다.

### 6. SpecFlux와 Complex의 보완성 시험 (보류)

HFC만으로 놓치는 `7207ms` weak-up을 대상으로 SpecFlux와 Complex descriptor를 같은 라벨과
raw onset 기준으로 비교하는 계획이다. 다만 현재는 onset 후보 부족보다 onset 시각과 chord
window anchoring이 병목으로 판단되므로, 이 비교는 보류한다.

- 각 descriptor의 raw TP/FP/FN과 `7207ms` 검출 여부를 기록한다.
- HFC의 raw TP를 유지하면서 다른 descriptor만 잡는 onset이 있는지 확인한다.
- 보완성이 확인된 경우에만 HFC를 기준 detector로 두고 보조 후보 결합 방식을 검토한다.

### 7. onset 분석 창 크기 비교 (보류)

현재 onset buffer는 1024 sample(44.1kHz에서 약 23ms), hop은 128 sample이다.
buffer를 512 sample(약 12ms)으로 줄여 빠른 스트로크 attack이 이전 스트로크와 덜 섞이는지
확인하는 계획이다. 다만 anchoring 실험보다 우선하지 않는다.

### 8. 필요 시 target-aware 보조 detector 설계 (보류)

energy 단독 검출은 채택하지 않는다. 이후에도 HFC가 실제 스트로크를 누락하면,
저·중역의 짧은 RMS 상승을 보조 후보로 사용하고, 이후 G5 fundamental 판정이 통과할 때만
타깃 진행을 허용하는 방식을 검토한다.

이 단계는 custom detector와 상태 처리 변경이 필요하다. 현재 HFC detector가 충분하다는
결론을 바꾸는 새로운 입력 증거가 생길 때만 다시 검토한다.

| threshold | raw / started |  TP |  FP |  FN | Recall | Precision |        F1 |
| --------: | ------------: | --: | --: | --: | -----: | --------: | --------: |
|      0.15 |       22 / 21 |  20 |   1 |   4 |  83.3% |     95.2% |     88.9% |
|      0.10 |       23 / 21 |  20 |   1 |   4 |  83.3% |     95.2% |     88.9% |
|      0.05 |       24 / 22 |  21 |   1 |   3 |  87.5% |     95.5% |     91.3% |
|      0.05 |       24 / 22 |  21 |   1 |   3 |  87.5% |     95.5% |     91.3% |
|      0.04 |       25 / 23 |  22 |   1 |   2 |  91.7% |     95.7% | **93.6%** |
|      0.03 |       25 / 23 |  22 |   1 |   2 |  91.7% |     95.7% | **93.6%** |
