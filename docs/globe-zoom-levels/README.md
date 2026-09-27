# Globe label zoom levels / 지구본 이름 표시 줌 단계

## 한국어

지구본 확대 단계(0 = 가장 멀리, 5 = 가장 가까이)에 따라 이름을 언제부터 보일지 정할 수 있습니다.
값을 적지 않으면 순정 동작 그대로입니다.

| 대상 | 적는 곳 | 기본값 |
|---|---|---|
| 펀딩 국가 이름 | `countries:` 항목의 `zoomLevel` | 2 |
| 추가 지구본 라벨 | `extraGlobeLabels:` 항목의 `zoomLevel` | 0 (기존과 같음) |
| 도시 (마커 + 이름) | 지역 `missionZones`의 도시 점 항목 7번째 값 | 3 |

```yaml
countries:
  - type: STR_NIGERIA
    zoomLevel: 3          # 3단계부터 국가 이름 표시

regions:
  - type: STR_NORTH_AFRICA
    missionZones:
      # ... (다른 구역은 그대로 다시 적어야 함: missionZones는 통째로 교체됨)
      -
        - [3.125, 3.125, -6.5, -6.5, -1, STR_LAGOS, 4]   # 4단계부터 라고스 마커+이름
        - [31.25, 31.25, -30, -30, -1, STR_CAIRO]        # 생략 = 3 (순정)
```

- 범위는 0~5. 벗어나면 가까운 값으로 고치고 로그에 경고를 남깁니다 (국가·도시만; extraGlobeLabels 값은 예전처럼 그대로 씁니다).
- 도시는 마커와 이름이 함께 나타납니다. 기지 이름은 순정처럼 3단계부터입니다.
- `missionZones`는 목록 전체가 교체되므로 구역 순서와 내용을 원본과 같게 유지하세요 (미션이 구역 번호를 씁니다).
- 예제: `test-mod/globe_zoom_test` (나이지리아 3, 라고스 4, 킨샤사 5).
- 일반 OXCE에서는 `missionZones` 7번째 값과 펀딩 국가 `zoomLevel`이 오류 없이 무시됩니다 (OXCE 8.7.1 코드 기준).

## English

Choose from which globe zoom level (0 = farthest, 5 = closest) a name is shown.
Without a value the original behaviour is kept.

| What | Where | Default |
|---|---|---|
| Funding country name | `zoomLevel` of a `countries:` entry | 2 |
| Extra globe label | `zoomLevel` of an `extraGlobeLabels:` entry | 0 (unchanged) |
| City (marker + name) | 7th value of a city point in a region's `missionZones` | 3 |

- Range 0-5; out-of-range values are clamped with a log warning (countries and cities; extraGlobeLabels values are used as before).
- A city's marker and name appear together. Base names stay at zoom 3 (original).
- `missionZones` replaces the whole list: keep the zones in the original order (missions refer to zone numbers).
- Example: `test-mod/globe_zoom_test` (Nigeria 3, Lagos 4, Kinshasa 5).
- Plain OXCE ignores the 7th `missionZones` value and a funding country's `zoomLevel` without errors (checked against the OXCE 8.7.1 code).
