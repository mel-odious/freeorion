#define PHOENIX_LIMIT 11
#define BOOST_RESULT_OF_NUM_ARGS PHOENIX_LIMIT

#include "CommonParams.h"
#include "Label.h"
#include "ConditionParserImpl.h"
#include "ValueRefParser.h"
#include "EnumParser.h"
#include "../universe/Condition.h"

#include <boost/spirit/include/phoenix.hpp>

namespace phoenix = boost::phoenix;

namespace parse { namespace detail {
    struct rules {
        rules() {
            const parse::lexer& tok = parse::lexer::instance();

            const parse::value_ref_parser_rule<double>::type& double_value_ref =    parse::value_ref_parser<double>();
            const parse::value_ref_parser_rule< int >::type& flexible_int_ref =     parse::value_ref_parser_flexible_int();

            qi::_1_type _1;
            qi::_r1_type _r1;
            qi::_a_type _a;
            qi::_r2_type _r2;
            qi::_val_type _val;
            qi::eps_type eps;
            using phoenix::new_;
            using phoenix::construct;
            using phoenix::insert;

            producible
                =   tok.Unproducible_ [ _val = false ]
                |   tok.Producible_ [ _val = true ]
                |   eps [ _val = true ]
                ;

            location
                =    parse::label(Location_token) > parse::detail::condition_parser [ _r1 = _1 ]
                |    eps [ _r1 = new_<Condition::All>() ]
                ;

            consumption
                =   parse::label(Consumption_token)
                >   '['
                >  *(
                        consumable_meter(_r1)
                    |   consumable_special(_r2)
                    )
                >   ']'
                |   consumable_meter(_r1)
                |   consumable_special(_r2)
            ;

            typedef std::map<std::string, ValueRef::ValueRefBase<double>*>::value_type special_consumable_map_value_type;
            consumable_special
                =   tok.Special_
                >   parse::label(Name_token)        > tok.string [ _a = _1 ]
                >   parse::label(Consumption_token) > double_value_ref
                [ insert(_r1, construct<special_consumable_map_value_type>(_a, _1)) ]
            ;

            typedef std::map<MeterType, ValueRef::ValueRefBase<double>*>::value_type meter_consumable_map_value_type;
            consumable_meter
                =   parse::non_ship_part_meter_type_enum() [ _a = _1 ]
                >   parse::label(Consumption_token) > double_value_ref
                [ insert(_r1, construct<meter_consumable_map_value_type>(_a, _1)) ]
            ;

            producible.name("Producible or Unproducible");
            location.name("Location");
            common.name("Consumables");
            consumable_special.name("Consumable Special");
            consumable_meter.name("Consumable Meter");

#if DEBUG_PARSERS
            debug(producible);
            debug(location);
            debug(common);
            debug(consumption);
            debug(con_special);
            debug(consumable_meter);
#endif
        }

        typedef boost::spirit::qi::rule<
            parse::token_iterator,
            void (std::map<MeterType, ValueRef::ValueRefBase<double>*>&),
            qi::locals<
                MeterType
            >,
            parse::skipper_type
        > consumable_meter_rule;

        typedef boost::spirit::qi::rule<
            parse::token_iterator,
            void (std::map<std::string, ValueRef::ValueRefBase<double>*>&),
            qi::locals<
                std::string
            >,
            parse::skipper_type
        > consumable_special_rule;

        producible_rule                 producible;
        location_rule                   location;
        part_hull_common_params_rule    common;
        consumption_rule                consumption;
        consumable_special_rule         consumable_special;
        consumable_meter_rule           consumable_meter;
    };

    rules& GetRules() {
        static rules retval;
        return retval;
    }

    const producible_rule& producible_parser()
    { return GetRules().producible; }

    const location_rule& location_parser()
    { return GetRules().location; }

    const consumption_rule& consumption_parser()
    { return GetRules().consumption; }

    const part_hull_common_params_rule& common_params_parser()
    { return GetRules().common; }

} }
