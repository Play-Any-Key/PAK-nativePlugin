# G5 gain-up 라벨 기반 onset 판정 결과

## 목적

`G5stroke_gainUp.mp3`의 실제 스트로크 라벨을 기준으로 onset 판정 실패 위치를
TP/FP/FN으로 분류한다.

- 입력: `assets/testSound/G5stroke_gainUp.mp3`
- 라벨: `G5stroke_label.csv` (24개 스트로크)
- onset: aubio `default` (HFC), threshold `0.2`
- 라벨 매칭 허용 범위: onset 시각 기준 ±75ms
- 실행: `G5fingeringTest.exe` 3회 반복

## 측정 기준

- **TP**: 판정 시작 onset이 미매칭 라벨의 ±75ms 안에 있음
- **FP**: 판정 시작 onset에 대응하는 라벨이 없음
- **FN**: 끝까지 대응 onset이 없는 라벨

테스트 세션에서는 chord 판정 실패 후에도 다음 target으로 진행한다. 따라서 이후
입력이 같은 target에 묶이지 않고 라벨 전체를 비교할 수 있다.

`detectedOnsets`는 aubio raw onset 수이고, TP/FP/FN은 실제로 판정을 시작해 이벤트로
나온 onset을 기준으로 계산한다. pending chord 판정 중에 버려진 raw onset은 이벤트가
없으므로 TP/FP/FN에 직접 포함되지 않는다.

## 반복 실행 결과

3회 모두 아래 결과와 FN 시각이 동일했다.

```text
16/24 targets correct
detected onset 19 / started judgment 18
chord pass 16 / chord fail 2
```

| 구분 | TP | FP | FN | 재현율 |
|---|---:|---:|---:|---:|
| 전체 | 17 | 1 | 7 | 70.8% |
| down | 12 | 0 | 2 | 85.7% |
| up | 5 | 0 | 5 | 50.0% |
| strong | 13 | 0 | 2 | 86.7% |
| weak | 4 | 0 | 5 | 44.4% |

- Precision: 94.4% (17 / 18)
- F1: 81.0%
- FP: `1861.7ms` (첫 라벨 `2030.0ms`보다 약 168ms 빠름)

## FN 목록

| 라벨 시각 | 방향 | 세기 |
|---:|---|---|
| 2936.0ms | up | weak |
| 4362.0ms | up | weak |
| 4553.0ms | down | strong |
| 4715.0ms | up | weak |
| 5791.0ms | up | weak |
| 5963.0ms | down | strong |
| 7207.0ms | up | weak |

## 분석

1. 현재 HFC onset은 `weak up`에 가장 취약하다. weak up 9개 중 5개가 FN이다.
2. `down strong`은 15개 중 13개 TP로 가장 안정적이다.
3. chord fail 2건은 onset TP인 weak-up 입력에서 발생했다. 이는 onset 누락이 아니라
   chord fundamental 판정 실패다.
4. raw onset은 19개지만 판정 시작은 18개다. chord settle 대기 중 raw onset 하나가
   버려지는 구조적 손실이 남아 있다.
5. `16/24 targets correct`는 라벨 기준 성공 수가 아니다. 첫 FP도 chord 판정을 통과해
   target 하나를 소비하므로, 이 실험의 평가는 TP/FP/FN 및 chord pass/fail을 분리해
   해석해야 한다.

## 다음 실험 우선순위

`weak up` onset 재현율을 높이는 조건을 비교한다. HFC threshold를 고정한 adaptive
spectral whitening 실험을 먼저 수행하고, 이후 buffer size를 1024에서 512 sample로
바꾸어 한 변수씩 TP/FP/FN 변화를 비교한다.
