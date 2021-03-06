﻿////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2021 Vladislav Trifochkin
//
// This file is part of [pfs-parser](https://github.com/semenovf/pfs-parser) library.
//
// Changelog:
//      2021.01.16 Initial version.
//      2021.02.07 Alpha release.
//
// TODO 1. Add Concepts.
//
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "error.hpp"
#include "../core_rules.hpp"
#include "../generator.hpp"
#include <bitset>
#include <type_traits>

#if __cplusplus > 201703L && __cpp_concepts >= 201907L
#   include <concepts>
#endif

namespace pfs {
namespace parser {
namespace abnf {

/**
 * [RFC 5234 - Augmented BNF for Syntax Specifications: ABNF] (https://tools.ietf.org/html/rfc5234)
 * [RFC 7405 - Case-Sensitive String Support in ABNF](https://tools.ietf.org/html/rfc7405)
 *
 * History:
 *
 * RFC 5234 obsoletes RFC 4234
 * RFC 4234 obsoletes RFC 2234
 */

enum parse_policy_flag {
    // Allow case sensitive sequence for rule name (default is case insensitive)
    // as quotation mark besides double quote
    // TODO Is not applicable yet
      allow_case_sensitive_rulenames

    , parse_policy_count
};

using parse_policy_set = std::bitset<parse_policy_count>;


// Convert sequence '1*DIGIT' to integer value.
// Returns on error:
//      {0, false} if non-digit character found;
//      {std::numeric_limits<long>::max(), false} if overflow occured.
template <typename ForwardIterator>
std::pair<long, bool> to_decimal_number (ForwardIterator first, ForwardIterator last)
{
    using char_type = typename std::remove_reference<decltype(*first)>::type;
    constexpr int radix = 10;
    long result = 0;
    long cutoff_value = std::numeric_limits<long>::min() / radix;
    long cutoff_limit = std::numeric_limits<long>::min() % radix;

    cutoff_value *= -1;
    cutoff_limit *= -1;

    for (; first != last; ++first) {
        auto digit = uint32_t(*first) - uint32_t(char_type('0'));

        if (digit < 0 || digit > 9) {
            // Character is not a digit
            return std::make_pair(long{0}, false);
        }

        if (result < cutoff_value
                || (result == cutoff_value && static_cast<uintmax_t>(digit) <= cutoff_limit)) {
            result *= radix;
            result += static_cast<long>(digit);
        } else {
            // Too big, overflow
            return std::make_pair(std::numeric_limits<long>::max(), false);
        }
    }

    return std::make_pair(result, true);
}

/**
 * @return @c true if @a ch is any 7-bit US-ASCII character, excluding NUL,
 *         otherwise @c false.
 *
 * @note Grammar
 * prose_value_char = %x20-3D / %x3F-7E
 */
template <typename CharType>
inline bool is_prose_value_char (CharType ch)
{
    auto a = uint32_t(ch) >= uint32_t(CharType('\x20'))
        && uint32_t(ch) <= uint32_t(CharType('\x3D'));

    return a || (uint32_t(ch) >= uint32_t(CharType('\x3F'))
        && uint32_t(ch) <= uint32_t(CharType('\x7E')));
}

/**
 * @brief Advance by @c prose value.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of ProseContext.
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * ProseContext {
 *     bool prose (ForwardIterator first, ForwardIterator last)
 * }
 *
 * @note Grammar
 * prose-val = "<" *(%x20-3D / %x3F-7E) ">"
 *             ; bracketed string of SP and VCHAR without angles
 *             ; prose description, to be used as last resort;
 *             ; %x3E - is a right bracket character '>'
 */
template <typename ForwardIterator, typename ProseContext>
bool advance_prose (ForwardIterator & pos, ForwardIterator last
    , ProseContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    auto p = pos;

    if (p == last)
        return false;

    if (*p != char_type('<'))
        return false;

    ++p;

    auto first_pos = p;

    while (p != last && is_prose_value_char(*p))
        ++p;

    if (p == last)
        return false;

    if (*p != char_type('>'))
        return false;

    auto success = ctx.prose(first_pos, p);

    ++p;

    return success && compare_and_assign(pos, p);
}

enum class number_flag {
    unspecified, binary, decimal, hexadecimal
};

/**
 * @brief Advance by @c number value.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of NumberContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * NumberContext {
 *     bool first_number (number_flag, ForwardIterator first, ForwardIterator last)
 *     bool last_number (number_flag, ForwardIterator first, ForwardIterator last)
 *     bool next_number (number_flag, ForwardIterator first, ForwardIterator last)
 * }
 *
 * @note Grammar
 * num-val = "%" (bin-val / dec-val / hex-val)
 * bin-val = "b" 1*BIT [ 1*("." 1*BIT) / ("-" 1*BIT) ] ; series of concatenated bit values or single ONEOF range
 * dec-val = "d" 1*DIGIT [ 1*("." 1*DIGIT) / ("-" 1*DIGIT) ]
 * hex-val = "x" 1*HEXDIG [ 1*("." 1*HEXDIG) / ("-" 1*HEXDIG) ]
 */
template <typename ForwardIterator, typename NumberContext>
bool advance_number (ForwardIterator & pos, ForwardIterator last
    , NumberContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    auto p = pos;

    if (p == last)
        return false;

    if (*p != char_type('%'))
        return false;

    ++p;

    if (p == last)
        return false;

    number_flag flag = number_flag::unspecified;
    bool (* advance_func)(ForwardIterator &, ForwardIterator) = nullptr;
    bool (* is_digit_func)(char_type) = nullptr;

    if (*p == char_type('x')) {
        advance_func = advance_hexdigit_chars;
        is_digit_func = is_hexdigit_char;
        flag = number_flag::hexadecimal;
    } else if (*p == char_type('d')) {
        advance_func = advance_digit_chars;
        is_digit_func = is_digit_char;
        flag = number_flag::decimal;
    } else if (*p == char_type('b')) {
        advance_func = advance_bit_chars;
        is_digit_func = is_bit_char;
        flag = number_flag::binary;
    } else {
        return false;
    }

    ++p;

    if (p == last)
        return false;

    auto first_pos = p;

    if (!advance_func(p, last))
        return false;

    auto success = ctx.first_number(flag, first_pos, p);

    if (p != last) {
        if (*p == char_type('-')) {
            ++p;

            // At least one digit character must exists
            if (!is_digit_func(*p))
                return false;

            first_pos = p;

            if (!advance_func(p, last))
                return false;

            success = success && ctx.last_number(flag, first_pos, p);

        } else if (*p == char_type('.')) {
            while (*p == char_type('.')) {
                ++p;

                // At least one digit character must exists
                if (!is_digit_func(*p))
                    return false;

                first_pos = p;

                if (!advance_func(p, last))
                    return false;

                success = success && ctx.next_number(flag, first_pos, p);
            }

            // Notify observer no more valid elements parsed
            success = success && ctx.last_number(flag, p, p);
        } else {
            success = success && ctx.last_number(flag, p, p);
        }
    } else {
        success = success && ctx.last_number(flag, p, p);
    }

    return success && compare_and_assign(pos, p);
}

/**
 * @brief Advance by quoted string.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of QuotedStringContext
 * @error @c unbalanced_quote if quote is unbalanced.
 * @error @c bad_quoted_char if quoted string contains invalid character.
 * @error @c max_length_exceeded if quoted string length too long.
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * QuotedStringContext {
 *     size_t max_quoted_string_length ()
 *     bool quoted_string (ForwardIterator first, ForwardIterator last)
 *     void error (error_code ec, ForwardIterator near_pos);
 * }
 *
 * @note Grammar
 * char-val = DQUOTE *(%x20-21 / %x23-7E) DQUOTE
 *                  ; quoted string of SP and VCHAR
 *                  ; without DQUOTE (%x22)
 */
template <typename ForwardIterator, typename QuotedStringContext>
bool advance_quoted_string (ForwardIterator & pos, ForwardIterator last
    , QuotedStringContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    auto p = pos;

    if (p == last)
        return false;

    if (!is_dquote_char(*p))
        return false;

    ++p;

    auto first_pos = p;

    if (p == last) {
        auto ec = make_error_code(errc::unbalanced_quote);
        ctx.error(ec, first_pos);
        return false;
    }

    size_t length = 0;
    size_t max_length = ctx.max_quoted_string_length();

    if (max_length == 0)
        max_length = std::numeric_limits<size_t>::max();

    // Parse quoted string of SP and VCHAR without DQUOTE
    while (p != last && !is_dquote_char(*p)) {
        if (!(is_visible_char(*p) || is_space_char(*p))) {
            auto ec = make_error_code(errc::bad_quoted_char);
            ctx.error(ec, p);
            return false;
        }

        if (length == max_length) {
            auto ec = make_error_code(errc::max_length_exceeded);
            ctx.error(ec, first_pos);
            return false;
        }

        ++length;
        ++p;
    }

    if (p == last) {
        auto ec = make_error_code(errc::unbalanced_quote);
        ctx.error(ec, first_pos);
        return false;
    }

    auto success = ctx.quoted_string(first_pos, p);

    ++p; // Skip DQUOTE

    return success && compare_and_assign(pos, p);
}

/**
 * @brief Advance by 'repeat' rule.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of RepeatContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * RepeatContext {
 *     bool repeat (long from, long to)
 *     void error (error_code ec, ForwardIterator near_pos)
 * }
 *
 * if first_from == last_from -> no low limit (*1)
 * if first_to == toLast -> no upper limit (1*)
 * if first_from == first_to && first_from != last_from -> is exact limit
 *
 * @note Grammar
 * repeat = 1*DIGIT / (*DIGIT "*" *DIGIT)
 */
template <typename ForwardIterator, typename RepeatContext>
bool advance_repeat (ForwardIterator & pos
    , ForwardIterator last
    , RepeatContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    bool success = true;
    auto p = pos;

    if (p == last)
        return false;

    ForwardIterator first_from = pos;
    ForwardIterator last_from  = pos;
    ForwardIterator first_to   = pos;
    ForwardIterator last_to    = pos;

    do {
        if (is_digit_char(*p)) {
            advance_digit_chars(p, last);
            last_from = p;

            // Success, is exact limit
            if (p == last) {
                first_to = first_from;
                last_to = last_from;
                break;
            }
        }

        if (*p == char_type('*')) {
            ++p;

            if (p == last) { // Success, second part is empty
                break;
            } else if (is_digit_char(*p)) {
                first_to = p;
                advance_digit_chars(p, last);
                last_to = p;
                break;
            } else { // Success, second part is empty
                break;
            }
        }
    } while (false);

    if (p != pos) {
        auto from = to_decimal_number(first_from, last_from);
        auto to = to_decimal_number(first_to, last_to);

        if (!from.second) {
            ctx.error(make_error_code(pfs::parser::errc::bad_repeat_range), first_from);
            return false;
        }

        if (!to.second) {
            ctx.error(make_error_code(pfs::parser::errc::bad_repeat_range), first_to);
            return false;
        }

        if (first_to == last_to)
            to.first = std::numeric_limits<long>::max();

        if (from.first > to.first) {
            ctx.error(make_error_code(pfs::parser::errc::bad_repeat_range), first_from);
            return false;
        }

        success = success && ctx.repeat(from.first, to.first);
    }

    return success && compare_and_assign(pos, p);
}

/**
 * @brief Advance by comment.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of CommentContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @note Grammar
 * comment = ";" *(< neither CR nor LF character >) CRLF
 *
 * This grammar replaces more strict one from RFC 5234:
 * comment = ";" *(WSP / VCHAR) CRLF
 */
template <typename ForwardIterator>
bool advance_comment (ForwardIterator & pos, ForwardIterator last)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    auto p = pos;

    if (p == last)
        return false;

    if (*p != char_type(';'))
        return false;

    ++p;

    ForwardIterator first_pos = p;

    while (p != last && !(is_cr_char(*p) || is_lf_char(*p))) {
        ++p;
    }

    if (p != last)
        advance_newline(p, last);

    return compare_and_assign(pos, p);
}

/**
 * @brief Advance by comment or new line.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of CommentContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @note Grammar
 * c-nl = comment / CRLF ; comment or newline
 */
template <typename ForwardIterator>
inline bool advance_comment_newline (ForwardIterator & pos, ForwardIterator last)
{
    return advance_newline(pos, last)
        || advance_comment(pos, last);
}

/**
 * @brief Advance by white space or group of comment or new line with white space.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of CommentContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @note Grammar
 * c-wsp =  WSP / (c-nl WSP)
 */
template <typename ForwardIterator>
inline bool advance_comment_whitespace (ForwardIterator & pos, ForwardIterator last)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    if (pos == last)
        return false;

    auto p = pos;

    if (is_whitespace_char(*p)) {
        ++p;
    } else if (advance_comment_newline(p, last)) {
        if (p == last)
            return false;

        if (! is_whitespace_char(*p))
            return false;

        ++p;
    } else {
        return false;
    }

    return compare_and_assign(pos, p);
}

//
// Helper function for advance_rulename() and advance_rule()
//
template <typename ForwardIterator>
bool advance_rulename_helper (ForwardIterator & pos, ForwardIterator last
    , ForwardIterator & rulename_first, ForwardIterator & rulename_last)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    auto p = pos;

    if (!is_alpha_char(*p))
        return false;

    ForwardIterator first_pos = p;

    ++p;

    while (p != last
        && (is_alpha_char(*p)
            || is_digit_char(*p)
            || *p == char_type('-'))) {
        ++p;
    }

    rulename_first = first_pos;
    rulename_last = p;

    return compare_and_assign(pos, p);
}

/**
 * @brief Advance by rule name.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of RulenameContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * RulenameContext {
 *     bool rulename (ForwardIterator first, ForwardIterator last)
 * }
 *
 * @note Grammar
 * rulename = ALPHA *(ALPHA / DIGIT / "-")
 */
template <typename ForwardIterator, typename RulenameContext>
bool advance_rulename (ForwardIterator & pos, ForwardIterator last
    , RulenameContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    if (pos == last)
        return false;

    ForwardIterator rulename_first {pos};
    ForwardIterator rulename_last {pos};

    if (!advance_rulename_helper(pos, last, rulename_first, rulename_last))
        return false;

    auto success = ctx.rulename(rulename_first, rulename_last);

    return success;
}

// Forward decalrations for advance_group() and advance_option()
// used in advance_element()
template <typename ForwardIterator, typename GroupContext>
bool advance_group (ForwardIterator &, ForwardIterator, GroupContext &);

template <typename ForwardIterator, typename OptionContext>
bool advance_option (ForwardIterator &, ForwardIterator, OptionContext &);

/**
 * @brief Advance by element.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of ElementContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * ElementContext extends RulenameContext
 *      , GroupContext
 *      , OptionContext
 *      , QuotedStringContext
 *      , NumberContext
 *      , ProseContext { }
 *
 * @note Grammar
 * element = rulename / group / option / char-val / num-val / prose-val
 */
template <typename ForwardIterator, typename ElementContext>
bool advance_element (ForwardIterator & pos, ForwardIterator last
    , ElementContext & ctx)
{
    if (pos == last)
        return false;

    return advance_rulename(pos, last, ctx)
        || advance_group(pos, last, ctx)
        || advance_option(pos, last, ctx)
        || advance_number(pos, last, ctx)
        || advance_quoted_string(pos, last, ctx)
        || advance_prose(pos, last, ctx);
}

/**
 * @brief Advance by repetition.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of RepetitionContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * RepetitionContext extends RepeatContext
 *      , ElementContext
 * {
 *      bool begin_repetition ()
 *      bool end_repetition (bool success)
 * }
 *
 * @note Grammar
 * repetition = [repeat] element
 */
template <typename ForwardIterator, typename RepetitionContext>
bool advance_repetition (ForwardIterator & pos, ForwardIterator last
    , RepetitionContext & ctx)
{
    if (pos == last)
        return false;

    auto success = ctx.begin_repetition();
    advance_repeat(pos, last, ctx);
    success = success && advance_element(pos, last, ctx);
    success = ctx.end_repetition(success) && success;

    return success;
}

/**
 * @brief Advance by concatenation.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of ConcatenationContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * ConcatenationContext extends RepetitionContext
 *      , CommentContext
 * {
 *      bool begin_concatenation ()
 *      bool end_concatenation (bool success)
 * }
 *
 * @note Grammar
 * concatenation = repetition *(1*c-wsp repetition)
 */
template <typename ForwardIterator, typename ConcatenationContext>
bool advance_concatenation (ForwardIterator & pos, ForwardIterator last
    , ConcatenationContext & ctx)
{
    if (pos == last)
        return false;

    auto success = ctx.begin_concatenation();

    // At least one repetition need
    success = success && advance_repetition(pos, last, ctx);

    // *(1*c-wsp repetition)
    success = success && advance_repetition_by_range(pos, last, unlimited_range()
        , [& ctx] (ForwardIterator & pos, ForwardIterator last) -> bool {

            auto p = pos;

            // 1*c-wsp
            if (! advance_repetition_by_range(p, last, make_range(1)
                    , [] (ForwardIterator & pos, ForwardIterator last) -> bool {
                        return advance_comment_whitespace(pos, last);
                    })) {
                return false;
            }

            if (! advance_repetition(p, last, ctx))
                return false;

            return compare_and_assign(pos, p);
        });

    success = ctx.end_concatenation(success) && success;

    return success;
}

/**
 * @brief Advance by alternation.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of AlternationContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * AlternationContext extends ConcatenationContext
 * {
 *      bool begin_alternation ()
 *      bool end_alternation ()
 * }
 *
 * @note Grammar
 * alternation = concatenation *(*c-wsp "/" *c-wsp concatenation)
 */
template <typename ForwardIterator, typename AlternationContext>
bool advance_alternation (ForwardIterator & pos, ForwardIterator last
    , AlternationContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    if (pos == last)
        return false;

    auto success = ctx.begin_alternation();

    success = success && advance_concatenation(pos, last, ctx);

    // _________________________________________________
    // |                                               |
    // *(*c-wsp "/" *c-wsp concatenation)              v
    success = success && advance_repetition_by_range(pos, last, unlimited_range()
        , [& ctx] (ForwardIterator & pos, ForwardIterator last) -> bool {
            auto p = pos;

            // *c-wsp
            while (advance_comment_whitespace(p, last))
                ;

            if (p == last)
                return false;

            if (*p != char_type('/'))
                return false;

            ++p;

            if (p == last)
                return false;

            // *c-wsp
            while (advance_comment_whitespace(p, last))
                ;

            if (! advance_concatenation(p, last, ctx))
                return false;

            return compare_and_assign(pos, p);
        });

    success = ctx.end_alternation(success) && success;

    return success;
}

/* Helper function
 *
 * Grammar
 * group  = "(" *c-wsp alternation *c-wsp ")"
 * option = "[" *c-wsp alternation *c-wsp "]"
 */
template <typename ForwardIterator, typename GroupContext>
bool advance_group_or_option (ForwardIterator & pos, ForwardIterator last
    , GroupContext & ctx)
{
    using char_type = typename std::remove_cv<typename std::remove_reference<decltype(*pos)>::type>::type;

    if (pos == last)
        return false;

    char_type closing_bracket {'\x0'};
    auto p = pos;

    if (*p == char_type('(')) {
        closing_bracket = char_type(')');
    } else if (*p == char_type('[')) {
        closing_bracket = char_type(']');
    } else {
        return false;
    }

    ++p;

    // *c-wsp
    while (advance_comment_whitespace(p, last))
        ;

    if (p == last)
        return false;

    if (! advance_alternation(p, last, ctx))
        return false;

    // *c-wsp
    while (advance_comment_whitespace(p, last))
        ;

    if (*p != closing_bracket)
        return false;

    ++p;

    return compare_and_assign(pos, p);
}

/**
 * @brief Advance by group.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of GroupContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * GroupContext extends AlternationContext
 * {
 *      void begin_group ()
 *      void end_group (bool success)
 * }
 *
 * @note Grammar
 * group  = "(" *c-wsp alternation *c-wsp ")"
 */
template <typename ForwardIterator, typename GroupContext>
bool advance_group (ForwardIterator & pos, ForwardIterator last
    , GroupContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    if (pos == last)
        return false;

    auto p = pos;

    if (*p != char_type('('))
        return false;

    auto success = ctx.begin_group();
    success = success && advance_group_or_option(pos, last, ctx);
    success = ctx.end_group(success) && success;

    return success;
}

/**
 * @brief Advance by option.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of OptionContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * OptionContext extends AlternationContext
 * {
 *      bool begin_option ()
 *      bool end_option (bool success)
 * }
 *
 * @note Grammar
 * option = "[" *c-wsp alternation *c-wsp "]"
 */
template <typename ForwardIterator, typename OptionContext>
bool advance_option (ForwardIterator & pos, ForwardIterator last
    , OptionContext & ctx)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    if (pos == last)
        return false;

    auto p = pos;

    if (*p != char_type('['))
        return false;

    auto success = ctx.begin_option();
    success = success && advance_group_or_option(pos, last, ctx);
    success = ctx.end_option(success) && success ;

    return success;
}

/**
 * @brief Advance by @c defined-as.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param is_incremental_alternatives On output - @c false if it is a basic
 *        rule definition, @c true if it is an incremental alternative.
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @note Grammar
 * defined-as = *c-wsp ("=" / "=/") *c-wsp
 *          ; basic rules definition and
 *          ; incremental alternatives
 */
template <typename ForwardIterator>
bool advance_defined_as (ForwardIterator & pos, ForwardIterator last
    , bool & is_incremental_alternatives)
{
    using char_type = typename std::remove_reference<decltype(*pos)>::type;

    if (pos == last)
        return false;

    is_incremental_alternatives = false;
    auto p = pos;

    // *c-wsp
    while (advance_comment_whitespace(p, last))
        ;

    if (p == last)
        return false;

    if (*p == char_type('=')) {
        ++p;

        if (p != last) {
            if (*p == char_type('/')) {
                ++p;
                is_incremental_alternatives = true;
            }
        }
    } else {
        return false;
    }

    // *c-wsp
    while (advance_comment_whitespace(p, last))
        ;

    return compare_and_assign(pos, p);
}

/**
 * @brief Advance by @c elements.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of ElementsContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
  * @par
 * ElementsContext extends AlternationContext
 * {}
 *
 * @note Grammar
 * elements = alternation *c-wsp
 */
template <typename ForwardIterator, typename ElementsContext>
bool advance_elements (ForwardIterator & pos, ForwardIterator last
    , ElementsContext & ctx)
{
    auto p = pos;

    if (!advance_alternation(p, last, ctx))
        return false;

    // *c-wsp
    while (advance_comment_whitespace(p, last))
        ;

    return compare_and_assign(pos, p);
}

/**
 * @brief Advance by @c rule.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of RuleContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * RuleContext extends RulenameContext
 *      , ElementsContext
 *      , DefinedAsContext
 *      , CommentContext
 * {
 *      bool begin_rule (ForwardIterator rulename_first
 *          , ForwardIterator rulename_last
 *          , bool is_incremental_alternatives);
 *      bool end_rule (ForwardIterator rulename_first
 *          , ForwardIterator rulename_last
 *          , is_incremental_alternatives
 *          , bool success);
 * }
 *
 * @note Grammar
 * rule = rulename defined-as elements c-nl
 *          ; continues if next line starts
 *          ; with white space
 */
template <typename ForwardIterator, typename RuleContext>
bool advance_rule (ForwardIterator & pos, ForwardIterator last
    , RuleContext & ctx)
{
    auto p = pos;

    if (pos == last)
        return false;

    ForwardIterator rulename_first {p};
    ForwardIterator rulename_last {p};

    if (!advance_rulename_helper(p, last, rulename_first, rulename_last))
        return false;

    bool is_incremental_alternatives = false;

    if (!advance_defined_as(p, last, is_incremental_alternatives))
        return false;

    auto success = ctx.begin_rule(rulename_first, rulename_last
        , is_incremental_alternatives);
    success = success && advance_elements(p, last, ctx);

    if (p != last)
        success = success && advance_comment_newline(p, last);

    while (success && advance_linear_whitespace(p, last))
        ;

    success = ctx.end_rule(rulename_first, rulename_last
        , is_incremental_alternatives
        , success) && success;

    return success && compare_and_assign(pos, p);
}

/**
 * @brief Advance by @c rulelist.
 *
 * @param pos On input - first position, on output - last good position.
 * @param last End of sequence position.
 * @param ctx Structure satisfying requirements of RulelistContext
 * @return @c true if advanced by at least one position, otherwise @c false.
 *
 * @par
 * RulelistContext extends RuleContext
 *      , CommentContext
 * {
 *      bool begin_document ()
 *      bool end_document (bool success)
 * }
 *
 * @note Grammar
 * rulelist = 1*( rule / (*c-wsp c-nl) )
 */
template <typename ForwardIterator, typename RulelistContext>
bool advance_rulelist (ForwardIterator & pos, ForwardIterator last
    , RulelistContext & ctx)
{
    auto success = ctx.begin_document();

    success = success && advance_repetition_by_range(pos, last, make_range(1)
        , [& ctx] (ForwardIterator & pos, ForwardIterator last) -> bool {

            auto p = pos;

            if (advance_rule(p, last, ctx)) {
                ;
            } else {
                // *c-wsp
                while (advance_comment_whitespace(p, last))
                    ;

                if (p != last) {
                    if (!advance_comment_newline(p, last))
                        return false;
                }
            }

            return compare_and_assign(pos, p);
        });

    success = ctx.end_document(success) && success;

    return success;
}

}}} // // namespace pfs::parser::abnf
