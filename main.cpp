#include "raylib.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

/**
 * @brief 固定長入力バッファの最大バイト数。
 *
 * @note Caller:
 * - 末尾の '\0' を含む容量である。
 * - 入力可能な数字数は kInputTextCapacity - 1。
 */
static constexpr std::size_t kInputTextCapacity = 32;

/**
 * @brief 数字ボタンの数。
 *
 * @note Caller:
 * - 0 から 9 までの UI ボタン数と一致させる。
 */
static constexpr std::size_t kDigitButtonCount = 10;

/**
 * @brief submit() の結果。
 *
 * @note Caller:
 * - UI 側でエラー表示を追加する場合、この戻り値を使って分岐する。
 */
enum class SubmitResult : uint8_t
{
  Ok,
  EmptyInput,
};

/**
 * @brief raylib の Window ライフタイムを管理する owner。
 *
 * @note Caller:
 * - InitWindow() / CloseWindow() の対応関係を保証する。
 * - コピーは禁止する。Window の所有者は常に 1 つだけにする。
 * - OS ウィンドウ作成は main 初期化境界で行う。
 */
class RaylibWindow
{
public:
  RaylibWindow(int width, int height, const char* title)
  {
    InitWindow(width, height, title);
  }

  RaylibWindow(const RaylibWindow&) = delete;
  RaylibWindow& operator=(const RaylibWindow&) = delete;

  ~RaylibWindow()
  {
    CloseWindow();
  }
};

/**
 * @brief 期待値計算アプリの状態。
 *
 * @note Caller:
 * - 入力バッファ、入力長、期待値、入力回数の不変条件はこの型が守る。
 * - 呼び出し側は公開メンバー関数だけで状態遷移させる。
 * - この型は外部リソースを所有しない。
 */
class ExpectedValueState
{
public:
  ExpectedValueState()
  {
    reset();
  }

  /**
   * @brief 数字 1 文字を入力バッファに追加する。
   *
   * @param[in] digit '0' から '9' までの数字文字。
   *
   * @retval true 入力バッファへ追加した。
   * @retval false digit が数字でない、またはバッファに空きがない。
   * @post true の場合だけ input_len_ が 1 増える。
   */
  bool push_digit(char digit)
  {
    if (digit < '0' || digit > '9') {
      return false;
    }

    if (input_len_ + 1 >= input_text_.size()) {
      return false;
    }

    input_text_[input_len_] = digit;
    input_len_ += 1;
    input_text_[input_len_] = '\0';

    return true;
  }

  /**
   * @brief 入力末尾の 1 文字を削除する。
   *
   * @post 入力が空でなければ input_len が 1 減る。
   */
  void backspace()
  {
    if (input_len_ <= 0) {
      return;
    }

    input_len_ -= 1;
    input_text_[input_len_] = '\0';
  }

  /**
   * @brief 入力値を報酬として確定し、逐次平均で期待値を更新する。
   *
   * @return 確定処理の結果。
   * @post 入力が空でなければ input_count_ が 1 増え、期待値は
   *       R += (reward - R) / n で更新される。
   * @note Caller: input_count_ が 0 の状態では割り算しない。不変条件はこの関数内で守る。
   */
  SubmitResult submit()
  {
    if (input_len_ == 0) {
      return SubmitResult::EmptyInput;
    }

    const double reward = std::strtod(input_text_.data(), nullptr);

    input_count_ += 1;

    // count 更新後の n を使うため、 n == 0 の割り算は発生しない。
    expected_value_ += (reward - expected_value_) / static_cast<double>(input_count_);

    clear_input();
    return SubmitResult::Ok;
  }

  /**
   * @brief アプリ状態を初期状態に戻す。
   *
   * @post 入力バッファ、期待値、入力回数が初期化される。
   */
  void reset()
  {
    clear_input();
    expected_value_ = 0.0;
    input_count_ = 0;
  }

  /**
   * @brief 現在の入力文字列を返す。
   *
   * @return null 終端された内部バッファへの読み取り専用借用。
   * @note Caller: 戻り値を ExpectedValueState のライフタイムより長く保存しない。
   */
  const char* input_text() const
  {
    return input_text_.data();
  }

  /**
   * @brief 現在の期待値を返す。
   *
   * @return 入力回数が 0 の場合は 0.0。そうでなければ逐次平均で更新済みの値。
   */
  double expected_value() const
  {
    return expected_value_;
  }

  /**
   * @brief 確定入力回数を返す。
   *
   * @return submit() が成功した回数。
   */
  uint32_t input_count() const
  {
    return input_count_;
  }

private:
  /**
   * @brief 入力バッファだけを空にする。
   *
   * @post input_text_ は空文字列になり、input_len_ は 0 になる。
   */
  void clear_input()
  {
    input_text_[0] = '\0';
    input_len_ = 0;
  }

private:
  std::array<char, kInputTextCapacity> input_text_;
  size_t input_len_;
  double expected_value_;
  uint32_t input_count_;
};

/**
 * @brief クリック可能な矩形ボタン。
 *
 * @note Caller:
 * - label は借用文字列であり、Button は所有しない。
 * - label のライフタイムは Button 使用中に有効である必要がある。
 */
class Button
{
public:
  Button() = default;

  Button(Rectangle rect, const char* label) : rect_(rect), label_(label) {}

  /**
   * @brief このボタンが今回のフレームで押されたかを返す。
   *
   * @retval true 左クリックが矩形内で押された。
   * @retval false 今回のフレームでは押されていない。
   */
  bool is_clicked() const
  {
    const Vector2 mouse = GetMousePosition();

    return CheckCollisionPointRec(mouse, rect_) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
  }

  /**
   * @brief ボタンを描画する。
   *
   * @note Caller: BeginDrawing() と EndDrawing() の間で呼ぶ。
   */
  void draw() const
  {
    DrawRectangleRec(rect_, LIGHTGRAY);
    DrawRectangleLinesEx(rect_, 2, GRAY);
    DrawText(label_, static_cast<int>(rect_.x + 16), static_cast<int>(rect_.y + 12), 22, BLACK);
  }

private:
  Rectangle rect_;
  const char* label_;
};

/**
 * @brief キーボード入力をアプリ状態に反映する。
 *
 * @param[in,out] state アプリ状態。呼び出し側が所有し、本関数は借用のみ行う。
 *
 * @pre state は nullptr ではない。
 * @note Caller: フレーム更新中に 1 回だけ呼ぶ。
 */
static void App_UpdateKeyboard(ExpectedValueState* state)
{
  for (int key = KEY_ZERO; key <= KEY_NINE; ++key) {
    if (IsKeyPressed(key)) {
      const char digit = static_cast<char>('0' + (key - KEY_ZERO));
      state->push_digit(digit);
    }
  }

  for (int key = KEY_KP_0; key <= KEY_KP_9; ++key) {
    if (IsKeyPressed(key)) {
      const char digit = static_cast<char>('0' + (key - KEY_KP_0));
      state->push_digit(digit);
    }
  }

  if (IsKeyPressed(KEY_BACKSPACE)) {
    state->backspace();
  }

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
    static_cast<void>(state->submit());
  }

  if (IsKeyPressed(KEY_R)) {
    state->reset();
  }
}

/**
 * @brief アプリ状態を描画する。
 *
 * @param[in] state アプリ状態。読み取り専用の借用。
 *
 * @pre state は nullptr ではない。
 * @note Caller: BeginDrawing() と EndDrawing() の間で呼ぶ。
 */
static void App_DrawState(const ExpectedValueState* state)
{
  std::array<char, 128> count_text;
  std::array<char, 128> expected_text;

  std::snprintf(count_text.data(), count_text.size(), "Count: %u", state->input_count());
  std::snprintf(expected_text.data(), expected_text.size(), "Expected Value: %.2f",
                state->expected_value());

  DrawText("Expected Value Calculator", 40, 30, 28, BLACK);

  DrawText("Input value:", 40, 90, 20, DARKGRAY);
  DrawRectangleLines(180, 80, 260, 40, GRAY);
  DrawText(state->input_text(), 190, 90, 22, BLACK);

  DrawText(count_text.data(), 40, 175, 22, BLACK);
  DrawText(expected_text.data(), 40, 210, 24, MAROON);

  DrawText("Keyboard: 0-9 input / Enter submit / R reset / Backspace delete", 40, 420, 16,
           DARKGRAY);
}

int main()
{
  RaylibWindow window(640, 480, "Expected Value Calculator");
  SetTargetFPS(60);

  ExpectedValueState state;

  std::array<Button, kDigitButtonCount> digit_buttons;
  std::array<std::array<char, 2>, kDigitButtonCount> digit_labels;

  for (std::size_t i = 0; i < digit_buttons.size(); ++i) {
    digit_labels[i][0] = static_cast<char>('0' + i);
    digit_labels[i][1] = '\0';

    const int col = static_cast<int>(i % 5);
    const int row = static_cast<int>(i / 5);

    digit_buttons[i] = Button(
        Rectangle{
            40.0f + static_cast<float>(col) * 70.0f,
            250.0f + static_cast<float>(row) * 60.0f,
            60.0f,
            45.0f,
        },
        digit_labels[i].data());
  }

  const Button submit_button(Rectangle{420.0f, 250.0f, 140.0f, 45.0f}, "Enter");

  const Button reset_button(Rectangle{420.0f, 310.0f, 140.0f, 45.0f}, "Reset");

  while (!WindowShouldClose()) {
    App_UpdateKeyboard(&state);

    for (size_t i = 0; i < 10; ++i) {
      if (digit_buttons[i].is_clicked()) {
        state.push_digit(static_cast<char>('0' + i));
      }
    }

    if (submit_button.is_clicked()) {
      static_cast<void>(state.submit());
    }

    if (reset_button.is_clicked()) {
      state.reset();
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    App_DrawState(&state);

    for (size_t i = 0; i < 10; ++i) {
      digit_buttons[i].draw();
    }

    submit_button.draw();
    reset_button.draw();

    EndDrawing();
  }

  return 0;
}
