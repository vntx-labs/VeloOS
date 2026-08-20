// calc.c - Sicherer Taschenrechner für VeloOS mit strikter Syntaxprüfung

volatile unsigned char* video_memory = (volatile unsigned char*)0xb8000;

// Vorwärtsdeklarationen
static inline unsigned char inb(unsigned short port);
static inline void outb(unsigned short port, unsigned char data);
void print_str(int row, int col, const char* str, unsigned char color);
void print_number(int row, int col, int num, unsigned char color);
char scancode_to_ascii(unsigned char scancode, int is_e0, int shift_active);

// Globaler Hardware-Status
int extended_e0 = 0;
int shift_pressed = 0;

// Rechner-Speicher (Variablen a-z und Ans)
int variables[26] = {0};
int ans_value = 0;

// 1. Einstiegspunkt für den Kernel/Linker
void _start() {

    asm volatile("mov $0x70000, %rsp");

    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video_memory[i] = ' ';
        video_memory[i + 1] = 0x17;
    }

    print_str(1, 5, "=== VELO OS SICHERER TASCHENRECHNER ===", 0x1E);
    print_str(3, 5, "Strikte Syntax: 10+2*2 | Vergleich: x+5<15", 0x1F);
    print_str(4, 5, "Variablen setzen: Shift+V, dann z.B. a=50", 0x1F);
    print_str(5, 5, "ESC = Kernel | Enter = Berechnen", 0x1A);
    print_str(8, 5, "Eingabe:                         ", 0x1F);

    char input_buf[32];
    int input_len = 0;
    int cursor_col = 14;
    int clear_on_next_key = 0;
    int var_assign_mode = 0;

    while (1) {
        if (inb(0x64) & 0x01) {
            unsigned char scancode = inb(0x60);
            
            if (scancode == 0xE0) {
                extended_e0 = 1;
                continue;
            }

            if (scancode == 0x01) { // ESC
                break;
            }

            if (scancode == 0x2A || scancode == 0x36) {
                shift_pressed = 1;
                continue;
            }
            if (scancode == 0xAA || scancode == 0xB6) {
                shift_pressed = 0;
                continue;
            }

            if (scancode & 0x80) {
                extended_e0 = 0;
                continue;
            }

            if (shift_pressed && scancode == 0x2F) { // Shift + V
                var_assign_mode = 1;
                print_str(8, 5, "VAR SET:                         ", 0x1B); 
                input_len = 0;
                cursor_col = 14;
                clear_on_next_key = 0;
                extended_e0 = 0;
                continue;
            }

            char c = scancode_to_ascii(scancode, extended_e0, shift_pressed);
            extended_e0 = 0; 

            if (c != 0) {
                if (clear_on_next_key && c != '\n') {
                    print_str(8, 5, "Eingabe:                         ", 0x1F);
                    input_len = 0;
                    cursor_col = 14;
                    clear_on_next_key = 0;
                    var_assign_mode = 0;
                }

                if (c == '\n') { // Enter
                    input_buf[input_len] = '\0';
                    print_str(11, 5, "                                         ", 0x1E);

                    if (var_assign_mode) {
                        if (input_len >= 3 && input_buf[0] >= 'a' && input_buf[0] <= 'z' && input_buf[1] == '=') {
                            int var_idx = input_buf[0] - 'a';
                            int var_val = 0;
                            for (int x = 2; x < input_len; x++) {
                                if (input_buf[x] >= '0' && input_buf[x] <= '9') {
                                    var_val = var_val * 10 + (input_buf[x] - '0');
                                }
                            }
                            variables[var_idx] = var_val;
                            print_str(11, 5, "Variable gespeichert!", 0x12);
                        } else {
                            print_str(11, 5, "FEHLER: Format a=Zahl nutzen!", 0x1C);
                        }
                        var_assign_mode = 0;
                    } 
                    else {
                        // SICHERER TOKEN- & PRECEDENCE-PARSER
                        // Wir erlauben maximal 3 Zahlen und 2 Operatoren (z.B. a + b * c oder a + b = c)
                        // Pufferüberläufe sind durch feste Array-Grenzen (32 Byte) und strikte Längenprüfung ausgeschlossen.
                        
                        int tokens_val[3] = {0, 0, 0};
                        char tokens_op[2] = {0, 0};
                        int val_count = 0;
                        int op_count = 0;
                        
                        int current_num = 0;
                        int has_current_num = 0;
                        int syntax_error = 0;
                        int div_by_zero = 0;

                        int i = 0;
                        while (i < input_len) {
                            char ch = input_buf[i];

                            // Leerzeichen überspringen
                            if (ch == ' ') {
                                i++;
                                continue;
                            }

                            // Ans-Erkennung
                            if ((ch == 'A' || ch == 'a') && (i + 2 < input_len) && 
                                (input_buf[i+1] == 'N' || input_buf[i+1] == 'n') && 
                                (input_buf[i+2] == 'S' || input_buf[i+2] == 's')) {
                                current_num = ans_value;
                                has_current_num = 1;
                                i += 3;
                                continue;
                            }

                            // Variablen-Erkennung (a-z)
                            if (ch >= 'a' && ch <= 'z') {
                                current_num = variables[ch - 'a'];
                                has_current_num = 1;
                                i++;
                                continue;
                            }

                            // Ziffern einlesen
                            if (ch >= '0' && ch <= '9') {
                                current_num = current_num * 10 + (ch - '0');
                                has_current_num = 1;
                                i++;
                                continue;
                            }

                            // Operatoren (+, -, *, /, =, <, >)
                            if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '=' || ch == '<' || ch == '>') {
                                if (!has_current_num) {
                                    syntax_error = 1; // Operator ohne vorherige Zahl
                                    break;
                                }
                                if (val_count < 3) {
                                    tokens_val[val_count++] = current_num;
                                }
                                current_num = 0;
                                has_current_num = 0;

                                if (op_count < 2) {
                                    tokens_op[op_count++] = ch;
                                } else {
                                    syntax_error = 1; // Zu viele Operatoren
                                    break;
                                }
                                i++;
                                continue;
                            }

                            // Unbekanntes Zeichen
                            syntax_error = 1;
                            break;
                        }

                        // Letzte Zahl sichern
                        if (!syntax_error && has_current_num && val_count < 3) {
                            tokens_val[val_count++] = current_num;
                        } else if (!has_current_num) {
                            syntax_error = 1;
                        }

                        // Validierung der Token-Anzahl (Muss entweder 2 Werte / 1 Op oder 3 Werte / 2 Ops sein)
                        if (!syntax_error) {
                            if (val_count == 2 && op_count == 1) {
                                // Gültig: z.B. 10+2 oder x<10
                            } else if (val_count == 3 && op_count == 2) {
                                // Gültig: z.B. 10+2*2 oder x+5=15
                            } else {
                                syntax_error = 1;
                            }
                        }

                        if (syntax_error) {
                            print_str(11, 5, "FEHLER: Ungueltige Syntax!", 0x1C);
                        } 
                        else {
                            // PRÜFEN OB ES EIN VERGLEICH IST (=, <, >)
                            int is_comparison = 0;
                            for (int c_idx = 0; c_idx < op_count; c_idx++) {
                                if (tokens_op[c_idx] == '=' || tokens_op[c_idx] == '<' || tokens_op[c_idx] == '>') {
                                    is_comparison = 1;
                                }
                            }

                            if (is_comparison) {
                                // Vergleiche dürfen keine Mehrfach-Vergleiche sein und müssen klar strukturiert sein
                                if (op_count != 1 && !(op_count == 2 && (tokens_op[0] == '+' || tokens_op[0] == '-' || tokens_op[0] == '*' || tokens_op[0] == '/') && (tokens_op[1] == '=' || tokens_op[1] == '<' || tokens_op[1] == '>'))) {
                                    print_str(11, 5, "FEHLER: Ungueltiger Vergleich!", 0x1C);
                                } else {
                                    int left_val = 0;
                                    char comp_op = 0;
                                    int right_val = 0;

                                    if (op_count == 1) {
                                        left_val = tokens_val[0];
                                        comp_op = tokens_op[0];
                                        right_val = tokens_val[1];
                                    } else {
                                        // Z.B. x + 5 = 15 -> Erst linke Seite berechnen unter Beachtung von Punkt-vor-Strich
                                        int l1 = tokens_val[0];
                                        char op_l = tokens_op[0];
                                        int l2 = tokens_val[1];
                                        comp_op = tokens_op[1];
                                        right_val = tokens_val[2];

                                        if (op_l == '*') left_val = l1 * l2;
                                        else if (op_l == '/') {
                                            if (l2 == 0) div_by_zero = 1;
                                            else left_val = l1 / l2;
                                        }
                                        else if (op_l == '+') left_val = l1 + l2;
                                        else if (op_l == '-') left_val = l1 - l2;
                                    }

                                    print_str(11, 5, "Ergebnis: ", 0x1E);
                                    if (div_by_zero) {
                                        print_str(11, 15, "FEHLER: Div Null!", 0x1C);
                                    } else {
                                        int condition_met = 0;
                                        if (comp_op == '=') condition_met = (left_val == right_val);
                                        else if (comp_op == '<') condition_met = (left_val < right_val);
                                        else if (comp_op == '>') condition_met = (left_val > right_val);

                                        if (condition_met) {
                                            print_str(11, 15, "Richtig! (1)", 0x12);
                                            ans_value = 1;
                                        } else {
                                            print_str(11, 15, "Falsch! (0)", 0x1C);
                                            ans_value = 0;
                                        }
                                    }
                                }
                            } 
                            else {
                                // NORMALER RECHENMODUS MIT PUNKT-VOR-STRICH
                                int final_result = 0;

                                if (op_count == 1) {
                                    char op = tokens_op[0];
                                    int a = tokens_val[0];
                                    int b = tokens_val[1];
                                    if (op == '+') final_result = a + b;
                                    else if (op == '-') final_result = a - b;
                                    else if (op == '*') final_result = a * b;
                                    else if (op == '/') {
                                        if (b == 0) div_by_zero = 1;
                                        else final_result = a / b;
                                    }
                                } 
                                else if (op_count == 2) {
                                    // 3 Werte, 2 Operatoren (z.B. 10 + 2 * 2)
                                    int a = tokens_val[0];
                                    char op1 = tokens_op[0];
                                    int b = tokens_val[1];
                                    char op2 = tokens_op[1];
                                    int c_val = tokens_val[2];

                                    // Prüfen ob op2 Punktrechnung ist (* oder /) während op1 Strichrechnung ist (+ oder -)
                                    if ((op1 == '+' || op1 == '-') && (op2 == '*' || op2 == '/')) {
                                        // Zweiter Teil zuerst ausführen
                                        int temp_res = 0;
                                        if (op2 == '*') temp_res = b * c_val;
                                        else {
                                            if (c_val == 0) div_by_zero = 1;
                                            else temp_res = b / c_val;
                                        }
                                        if (!div_by_zero) {
                                            if (op1 == '+') final_result = a + temp_res;
                                            else final_result = a - temp_res;
                                        }
                                    } else {
                                        // Von links nach rechts abarbeiten
                                        int temp_res = 0;
                                        if (op1 == '+') temp_res = a + b;
                                        else if (op1 == '-') temp_res = a - b;
                                        else if (op1 == '*') temp_res = a * b;
                                        else if (op1 == '/') {
                                            if (b == 0) div_by_zero = 1;
                                            else temp_res = a / b;
                                        }

                                        if (!div_by_zero) {
                                            if (op2 == '+') final_result = temp_res + c_val;
                                            else if (op2 == '-') final_result = temp_res - c_val;
                                            else if (op2 == '*') final_result = temp_res * c_val;
                                            else if (op2 == '/') {
                                                if (c_val == 0) div_by_zero = 1;
                                                else final_result = temp_res / c_val;
                                            }
                                        }
                                    }
                                }

                                print_str(11, 5, "Ergebnis: ", 0x1E);
                                if (div_by_zero) {
                                    print_str(11, 15, "FEHLER: Div Null!", 0x1C);
                                } else {
                                    print_number(11, 15, final_result, 0x1E);
                                    ans_value = final_result;
                                }
                            }
                        }
                    }

                    clear_on_next_key = 1;

                } else if (c == '\b') {
                    if (input_len > 0) {
                        input_len--;
                        cursor_col--;
                        video_memory[(8 * 80 + cursor_col) * 2] = ' ';
                        video_memory[(8 * 80 + cursor_col) * 2 + 1] = 0x1F;
                    }
                } else {
                    if (input_len < 31) {
                        input_buf[input_len++] = c;
                        video_memory[(8 * 80 + cursor_col) * 2] = c;
                        video_memory[(8 * 80 + cursor_col) * 2 + 1] = (var_assign_mode) ? 0x0B : 0x0F;
                        cursor_col++;
                    }
                }
            }
        }
        outb(0x80, 0);
    }

    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video_memory[i] = ' ';
        video_memory[i + 1] = 0x07;
    }
}

void print_str(int row, int col, const char* str, unsigned char color) {
    int pos = (row * 80 + col) * 2;
    for (int i = 0; str[i] != '\0'; i++) {
        video_memory[pos] = str[i];
        video_memory[pos + 1] = color;
        pos += 2;
    }
}

void print_number(int row, int col, int num, unsigned char color) {
    char buf[16];
    int i = 14;
    buf[15] = '\0';
    int is_negative = 0;
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    if (num == 0) {
        buf[i--] = '0';
    } else {
        while (num > 0 && i >= 0) {
            int q = num / 10;
            int rem = num % 10;
            buf[i--] = '0' + rem;
            num = q;
        }
    }
    if (is_negative && i >= 0) {
        buf[i--] = '-';
    }
    print_str(row, col, &buf[i + 1], color);
}

char scancode_to_ascii(unsigned char scancode, int is_e0, int shift_active) {
    if (is_e0) {
        switch(scancode) {
            case 0x35: return '/';
            case 0x1C: return '\n';
            default: return 0;
        }
    }
    if (shift_active) {
        switch(scancode) {
            case 0x08: return '/'; 
            case 0x0B: return '='; 
            case 0x1B: return '*'; 
            case 0x56: return '>'; 
        }
    }
    switch(scancode) {
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x56: return '<'; 
        
        case 0x1E: return 'a';
        case 0x30: return 'b';
        case 0x2E: return 'c';
        case 0x20: return 'd';
        case 0x12: return 'e';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x17: return 'i';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x32: return 'm';
        case 0x31: return 'n';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x10: return 'q';
        case 0x13: return 'r';
        case 0x1F: return 's';
        case 0x14: return 't';
        case 0x16: return 'u';
        case 0x2F: return 'v';
        case 0x11: return 'w';
        case 0x2D: return 'x';
        case 0x15: return 'y';
        case 0x2C: return 'z';

        case 0x4F: return '1';
        case 0x50: return '2';
        case 0x51: return '3';
        case 0x4B: return '4';
        case 0x4C: return '5';
        case 0x4D: return '6';
        case 0x47: return '7';
        case 0x48: return '8';
        case 0x49: return '9';
        case 0x52: return '0';

        case 0x1B: return '+';  
        case 0x35: return '-';  
        case 0x4E: return '+';  
        case 0x4A: return '-';  
        case 0x37: return '*';  
        case 0x39: return ' ';  
        case 0x1C: return '\n'; 
        case 0x0E: return '\b'; 
        default: return 0;
    }
}

static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}