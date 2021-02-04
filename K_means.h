#include <concepts>
#include <array>
#include <vector>
#include <algorithm>
#include <random>
#include <numeric>
#include <optional>
#include <ranges>
#include <range/v3/view/zip.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/map.hpp> //ranges::views::values
#include <fmt/ranges.h>
#define FWD(x) static_cast<decltype(x)&&>(x)

using fmt::print;

namespace kmn{

using size_type = std::size_t;

namespace rn = std::ranges;
namespace rnv = rn::views;
namespace r3 = ranges; //TODO: This should be rv3 as in "range v3" //used when std::ranges bugs
namespace rv3 = r3::views; //TODO: This should be rv3v

using rn::range_value_t;
using rnv::filter;
using rnv::keys;
using rnv::values;
using rv3::zip;

using std::declval;
using std::vector;

template<typename T>
concept arithmetic = std::integral<T> or std::floating_point<T>;

template<arithmetic T, size_type D> struct DataPoint;

namespace hlpr
{ /************************ select_centroid ******************************/
  //if T is integral, picks a floating value type for the centroid
  template<typename T, size_type D>
  struct select_centroid
  { using type = DataPoint<double, D>; };  
  //if T is a floating point type, the centroid's value type matches it
  template<std::floating_point T, size_type D>
  struct select_centroid<T, D>
  { using type = DataPoint<T, D>; };  
  //T is pre-constrained to being an arithmetic type in DataPoint
  template<typename T, size_type D>
  using select_centroid_t = typename select_centroid<T, D>::type;
  //centroid_t: Terser alias
  template<typename PT_VALUE_T, size_type D>
  using centroid_t = select_centroid_t<PT_VALUE_T, D>;
  /************************* is_data_point *******************************/
  template<typename T>
  struct is_data_point : std::false_type {};
  template<typename T, size_type D>
  struct is_data_point<DataPoint<T, D>> : std::true_type {};
  template<typename T>
  inline constexpr bool is_data_point_v = is_data_point<T>::value;
  /************************ data_point_size ******************************/
  template<typename T>
  struct data_point_size;
  template<typename T, size_type D>
  struct data_point_size<DataPoint<T, D>> : std::integral_constant<size_type, D> {};
  template<typename T>
  inline constexpr size_type data_point_size_v = data_point_size<T>::value;
  /************************** data_point_t *******************************/
  template<typename R>
  using data_point_t = range_value_t<R>;
  /************************** point_value_t ******************************/
  template<typename R>
  using point_value_t = data_point_t<R>::value_type;
  /***************************** CONCEPTS ********************************/
  template<typename R>
  concept data_points_range = is_data_point_v<range_value_t<R>>;
  template<typename R>
  concept unsigned_range = std::unsigned_integral<range_value_t<R>>;
}
using namespace hlpr;

//Class template DataPoint
template<arithmetic T, size_type D>
struct DataPoint final : private std::array<T, D> {
    using typename std::array<T, D>::value_type;
    using std::array<T, D>::begin;
    using std::array<T, D>::end;
    using std::array<T, D>::cbegin;
    using std::array<T, D>::cend;
    using std::array<T, D>::operator[];
    using std::array<T, D>::size;

    constexpr DataPoint() noexcept = default;
    constexpr DataPoint(DataPoint const&) noexcept = default;
    constexpr DataPoint& operator=(DataPoint const&) noexcept = default;
    constexpr DataPoint(std::convertible_to<value_type> auto... coords) noexcept
        requires (sizeof...(coords) == D)
        : std::array<T, D>{coords...} {}
    
    friend constexpr bool operator<=>(DataPoint const&, DataPoint const&) = default;
    
    constexpr auto operator+(DataPoint const& rhs) const
    { DataPoint res;
      rn::transform(rhs, *this, res.begin(), std::plus{});
      return res;
    }

    //operator/ overload for floating point value type; result's value type matches it
    constexpr auto operator/(arithmetic auto n) const
        requires (std::floating_point<value_type>)
    { DataPoint<value_type, D> res;
      rn::transform(*this, res.begin(),
      [&n](value_type e){ return e / static_cast<value_type>(n); });
      return res;
    }

    //operator/ overload for integer T => result's value type = double
    constexpr auto operator/(arithmetic auto n) const
    { DataPoint<double, D> res;
      rn::transform(*this, res.begin(),
                    [&n](value_type e)
                    { return e / static_cast<double>(n); });
      return res;
    }

    template<std::floating_point U>
    constexpr explicit operator DataPoint<U, D>() const
    { DataPoint<double, D> res;
      rn::transform(*this, res.begin(),
                    [](value_type num)
                    { return static_cast<U>(num); });
      return res;
    }
};
//DataPoint deduction guide
template<arithmetic T, typename... Us>
DataPoint(T, Us...) -> DataPoint<T, sizeof...(Us) + 1>;

//sqr_dist: computes euclidean square distance between two data points
template<typename T1, typename T2, size_type D>
constexpr auto
sqr_distance(DataPoint<T1,D> const& dp1,
             DataPoint<T2,D> const& dp2)
{    return std::transform_reduce(
            dp1.cbegin(), dp1.cend(), dp2.cbegin(), 0,
            [](T1 a, T2 b){ return a + b; },
            [](T1 a, T2 b){ return (a - b)*(a - b); });
};
//distance_from: Function Object Comparator
//               of distances from two points to a reference point
template<typename T, size_type D>
struct distance_from
{ using ref_point_t = DataPoint<T, D>;
  ref_point_t m_pt;
    
  distance_from() = delete;
  distance_from& operator=(distance_from const&) = delete;
  constexpr distance_from(ref_point_t const& pt) : m_pt{pt} {}
  
  template<typename U>
  constexpr auto operator()
  (DataPoint<U, D> const& c1, DataPoint<U, D> const& c2)
  const -> bool
  { return sqr_distance(c1, m_pt) < sqr_distance(c2, m_pt); }
};

template<typename PTS_R>
using centroid_type =
centroid_t<point_value_t<PTS_R>,
           data_point_size_v<data_point_t<PTS_R>>>;

template<typename PTS_R>
using indexed_centroids_t =
vector<std::pair<size_type, centroid_type<PTS_R>>>;

template<typename PTS_R>
auto init_centroids(PTS_R&& data_points, size_type k)
-> indexed_centroids_t<PTS_R>
{
  using PT_VALUE_T = point_value_t<PTS_R>;
  
  //TODO: reserve-init these and have rn::sample write to a back inserter
  vector<size_type> ids(k);
  vector<centroid_type<PTS_R>> centroids(k);
  auto indexed_centroids = zip(ids, centroids);
  
  //Initialize centroid ids
  //TODO: Look into indexed_centroids | rv3::keys = rnv::iota(1, k + 1)
  rn::generate(indexed_centroids | rv3::keys,
               [n = 1]() mutable { return n++; });

  //Initialize centroids with a sample of K points from data_points
  if constexpr(std::floating_point<PT_VALUE_T>){
      rn::sample(FWD(data_points),
                 r3::begin(indexed_centroids | rv3::values),
                 k, std::mt19937{std::random_device{}()});
  } else {
      //data_points here has integral value types T
      //T needs to be a floating point type to match centroids' T
      //because centroids get updated with data_points' means
      //and a mean's result is a floating point type
      auto constexpr DIM = data_point_size_v< data_point_t<PTS_R> >;
      rn::sample(FWD(data_points)
                 | rnv::transform(
                   [](DataPoint<PT_VALUE_T, DIM> const& pt)
                   { return static_cast<DataPoint<double, DIM>>(pt); }),
                 //COMPILATION ERROR: Try reproducing in a minimal example
                 r3::begin(indexed_centroids | rv3::values),
                 k, std::mt19937{std::random_device{}()});
  }
  return { rn::begin(indexed_centroids), rn::end(indexed_centroids) };
}

//This is used in update_centroids and in k_means_result
struct match_id
{ size_type cent_id;
  constexpr bool operator()
  (auto const& indexed_point) const
  { return cent_id == get<0>(indexed_point); }
};

void update_centroids(auto&& data_points,
                      auto&& out_indices,
                      auto&& indexed_centroids)
{ auto constexpr mean_matching_points =
  [](auto&& data_points)
  { using data_point_t = range_value_t<decltype(data_points)>;
    size_type count{};
    //input range is a filter_view which is not a sized range
    //so an elements' count is used instead of size()
    auto const counted_sum = 
    [&count](data_point_t const& pt1, data_point_t const& pt2)
    { ++count; return pt1 + pt2; };
    //count is a side effect of counted_sum,
    //so sum must be calculated separately
    auto const sum = std::reduce(rn::begin(data_points),
                                 rn::end(data_points),
                                 data_point_t(), counted_sum);
    return sum / count;
  };

  rn::transform(indexed_centroids | rv3::keys,
                r3::begin( indexed_centroids | rv3::values),
                [&](auto const& cent_id)
                { return mean_matching_points(zip(FWD(out_indices), FWD(data_points))
                                              | rv3::filter(match_id{cent_id})
                                              | rv3::values);
                });
}

void index_points_by_centroids(auto&& out_indices,
                               auto&& data_points,
                               auto const& indexed_centroids)
{   auto constexpr dist_from_centroid =
    [](auto const& pt){ return distance_from{ pt }; };

    auto constexpr proj_centroid =
    [](auto const& id_pt){ return id_pt.second; };

    auto const find_id_nearest_centroid =
    [&](auto const& pt) 
    { //rn::borrowed_iterator has no operator->
      return (*rn::min_element(indexed_centroids,
                               dist_from_centroid(pt),
                               proj_centroid)).first;
    };
    //Find the ID of the nearest centroid to each data point,
    //and write it to the output range
    rn::transform(FWD(data_points),
                  std::begin(FWD(out_indices)),
                  find_id_nearest_centroid);
}

auto gen_cluster_sizes(auto const& out_indices, size_type k)
{   
  auto const count_indices =
  [&out_indices](size_type index)
  { return rn::count(out_indices, index); };
  
  return rnv::iota(std::size_t{ 1 }, k + 1)
         | rnv::transform(count_indices);
}

template <typename CENTROIDS_R, typename SIZES_R,
          typename INPUT_R, typename OUTPUT_R>
struct k_means_result
{ CENTROIDS_R centroids;
  SIZES_R cluster_sizes;
  INPUT_R points;
  OUTPUT_R out_indices;

  using indexed_range =
  decltype(zip(FWD(out_indices), FWD(points)));
  
  using filtered_indexed_range =
  decltype(declval<indexed_range>() 
           | rv3::filter(match_id{}));
  
  using filtered_range =
  decltype(declval<filtered_indexed_range>()
           | rv3::values);
  
  struct iterator
  { k_means_result& parent;
    size_type cluster_idx;
    struct cluster
    { range_value_t<CENTROIDS_R>& centroid;
      filtered_range satellites;
    };
    auto operator*() -> cluster
    { return { parent.centroids[cluster_idx],
               zip(FWD(parent.out_indices), FWD(parent.points))
               | rv3::filter(match_id{ cluster_idx + 1 })
               | rv3::values
             };
    }
    
    void operator++() { ++cluster_idx; }
    
    friend bool operator==(iterator lhs, iterator rhs)
    { return lhs.cluster_idx == rhs.cluster_idx; }
  };

  auto begin() -> iterator { return { *this, size_type{ 0 } }; }
  auto end() -> iterator { return { *this, cluster_sizes.size() }; }
};

void print_kmn_result(auto&& opt_kmn_result)
{ 
  if(auto&& kmn_result = FWD(opt_kmn_result))
    for(auto&& [centroid, satellites] : *FWD(kmn_result))    
    { print("Centroid: {}\n", FWD(centroid));    
      print("Satellites: {}\n\n", FWD(satellites));
    }
  else
    print("k_means call is invalid; returned std::nullopt.\n");
  
}

template<typename IDX_R>
using cluster_sizes_t =
decltype(gen_cluster_sizes(declval<IDX_R>(), declval<size_type>()));

template<typename PTS_R>
using centroids_t = vector<centroid_type<PTS_R>>;

template<typename PTS_R, typename IDX_R>
using k_means_impl_t =
k_means_result<centroids_t<PTS_R>,
               cluster_sizes_t<IDX_R>, PTS_R, IDX_R>;

template<typename PTS_R, typename IDX_R>
constexpr auto
k_means_impl(PTS_R&& data_points, IDX_R&& out_indices,
             size_type k, size_type n)
-> k_means_impl_t<PTS_R, IDX_R>
{ //Initialize centroids and their ids
  //TODO: See if codegen is better if init_centroids becomes a functor (or just ask first)
  //      index_points_by_centroids(FWD(out_indices), FWD(data_points),
  //                                indexed_centroids | rnv::transform(init_centroids));
  auto&& indexed_centroids = init_centroids(FWD(data_points), k);
  index_points_by_centroids(FWD(out_indices), FWD(data_points), FWD(indexed_centroids));  
  //Update the centroids with means, repeat n times
  while(n--)
  { update_centroids(FWD(data_points),
                     FWD(out_indices),
                     FWD(indexed_centroids));
  }
  
  auto&& centroids_rn = indexed_centroids | rv3::values;
  
  return { vector(r3::begin(centroids_rn), r3::end(centroids_rn)),
           gen_cluster_sizes(FWD(out_indices), k),
           FWD(data_points), FWD(out_indices)
         };
}

struct k_means_fn
{ template<data_points_range PTS_R, unsigned_range IDX_R>
  constexpr auto operator()
  (PTS_R&& data_points, //not mutated but a reference to it is returned;
                        //&& for referring or moving depending on value category
   IDX_R&& out_indices, //is an rvalue reference to handle rvalue arguments
                        //such as views-like objects
   size_type k, size_type n) const noexcept
  -> std::optional<k_means_impl_t<PTS_R, IDX_R>>
  { 
    if(k < 2) return std::nullopt;

    if(auto const pts_size = rn::distance(data_points);
       pts_size < k or pts_size != rn::size(out_indices))
    { return std::nullopt; }
    
    return { k_means_impl< PTS_R, IDX_R >
             (FWD(data_points), FWD(out_indices), k, n) };
  }
};

//k_means: Callable object that the user can call or pass around
constexpr inline k_means_fn k_means{};

} // namespace kmn

int main(){
    using std::array, std::vector, kmn::DataPoint;
    using kmn::print_kmn_result, kmn::k_means;

    //INPUT ranges
    auto const int_arr_df =
    array{DataPoint(1, 2, 3),    DataPoint(4, 5, 6),    DataPoint(7, 8, 9),
          DataPoint(10, 11, 12), DataPoint(13, 14, 15), DataPoint(16, 17, 18),
          DataPoint(19, 20, 21), DataPoint(22, 23, 24), DataPoint(25, 26, 27),
          DataPoint(28, 29, 30), DataPoint(31, 32, 33), DataPoint(34, 35, 36),
          DataPoint(37, 38, 39), DataPoint(40, 41, 42), DataPoint(43, 44, 45)};
    
    auto const double_arr_df =
    array{DataPoint(1., 2., 3.),    DataPoint(4., 5., 6.),    DataPoint(7., 8., 9.),
          DataPoint(10., 11., 12.), DataPoint(13., 14., 15.), DataPoint(16., 17., 18.),
          DataPoint(19., 20., 21.), DataPoint(22., 23., 24.), DataPoint(25., 26., 27.),
          DataPoint(28., 29., 30.), DataPoint(31., 32., 33.), DataPoint(34., 35., 36.),
          DataPoint(37., 38., 39.), DataPoint(40., 41., 42.), DataPoint(43., 44., 45.)};

    auto const float_arr_df =
    array{DataPoint(1.f, 2.f, 3.f),    DataPoint(4.f, 5.f, 6.f),    DataPoint(7.f, 8.f, 9.f),
          DataPoint(10.f, 11.f, 12.f), DataPoint(13.f, 14.f, 15.f), DataPoint(16.f, 17.f, 18.f),
          DataPoint(19.f, 20.f, 21.f), DataPoint(22.f, 23.f, 24.f), DataPoint(25.f, 26.f, 27.f),
          DataPoint(28.f, 29.f, 30.f), DataPoint(31.f, 32.f, 33.f), DataPoint(34.f, 35.f, 36.f),
          DataPoint(37.f, 38.f, 39.f), DataPoint(40.f, 41.f, 42.f), DataPoint(43.f, 44.f, 45.f)};

    auto const int_vec_df =
    vector{DataPoint(1, 2, 3),    DataPoint(4, 5, 6),    DataPoint(7, 8, 9),
           DataPoint(10, 11, 12), DataPoint(13, 14, 15), DataPoint(16, 17, 18),
           DataPoint(19, 20, 21), DataPoint(22, 23, 24), DataPoint(25, 26, 27),
           DataPoint(28, 29, 30), DataPoint(31, 32, 33), DataPoint(34, 35, 36),
           DataPoint(37, 38, 39), DataPoint(40, 41, 42), DataPoint(43, 44, 45)};

    //OUTPUT range
    vector<std::size_t> out_indices(int_arr_df.size());
    
    //CALL & DISPLAY RESULT
    std::size_t const k{ 4 }, n{ 10 };
    // k_means(int_arr_df, out_indices, k, n);
    print_kmn_result(k_means(int_arr_df, out_indices, k, n));
    //UNIT TEST: Ensure #satellites ==  #data_points

    return 0;
}
