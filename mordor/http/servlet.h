#ifndef __MORDOR_HTTP_SERVLET_H__
#define __MORDOR_HTTP_SERVLET_H__
// Copyright (c) 2010 - Mozy, Inc.

#include <boost/variant.hpp>

#include "../factory.h"
#include "../uri.h"

namespace Mordor {
namespace HTTP {

class ServerRequest;

class Servlet
{
public:
    typedef std::shared_ptr<Servlet> ptr;

public:
    virtual ~Servlet() {}

    virtual void request(std::shared_ptr<ServerRequest> request) = 0;
    void operator()(std::shared_ptr<ServerRequest> requestPtr)
    { request(requestPtr); }
};

class ServletFilter : public Servlet
{
public:
    typedef std::shared_ptr<ServletFilter> ptr;

public:
    ServletFilter(Servlet::ptr parent) : m_parent(parent) {}

    Servlet::ptr parent() { return m_parent; }

private:
    Servlet::ptr m_parent;
};

/// Dispatches different parts of the URI namespace to registered servlets
///
/// Supports vhosts - if the registered URI has an authority defined, it will
/// create a vhost for that authority, and only accept requests with the given
/// Host header.  Other or missing Host headers will fall back to a servlet
/// defined with no authority.
/// Supports wildcard - '*' can be used in URI segments to allow matching any
/// content in the segment
/// Differing schemes (http vs. https) are not currently supported; the scheme
/// is currently ignored
/// URI match rule:
/// * authority match first, if no authority matches, fall back to rules in no authority
/// - non-wildcard character matching rule:
///   * longest path match first
///   * the same path is not allowed to register more than once, otherwise
///     assertion will be triggered.
/// - wildcard character matching rule:
///   * longest path match first
///   * if there is more than one path matches, following precedence will be considered
///     - non-wildcard path,
///     - wildcard path with right-most postion of the left-most '*'
class ServletDispatcher : public Servlet
{
private:
    typedef boost::variant<std::shared_ptr<Servlet>,
            std::function<Servlet *()> > ServletOrCreator;
    typedef std::map<URI::Path, ServletOrCreator> ServletPathMap;
    typedef ServletPathMap ServletWildcardPathMap;
    typedef std::pair<ServletPathMap, ServletWildcardPathMap> ServletPathMapPair;
    typedef std::map<URI::Authority, ServletPathMapPair> ServletHostMap;
public:
    typedef std::shared_ptr<ServletDispatcher> ptr;

public:
    ServletDispatcher(bool enableWildcard = false)
        : m_enableWildcard(enableWildcard)
    {}

public:
    /// Use to register a servlet that can share the same Servlet object every
    /// time (saves a std::bind and heap allocation for every request)
    void registerServlet(const URI &uri, std::shared_ptr<Servlet> servlet)
    { registerServlet(uri, ServletOrCreator(servlet)); }

    template <class T>
    void registerServlet(const URI &uri)
    {
        typedef Creator<Servlet, T> CreatorType;
        std::shared_ptr<CreatorType> creator(new CreatorType());
        registerServlet(std::bind(&CreatorType::create0, creator));
    }
    template <class T, class A1>
    void registerServlet(const URI &uri, A1 a1)
    {
        typedef Creator<Servlet, T, A1> CreatorType;
        std::shared_ptr<CreatorType> creator(new CreatorType());
        registerServlet(uri, std::bind(&CreatorType::create1, creator, a1));
    }
    template <class T, class A1, class A2>
    void registerServlet(const URI &uri, A1 a1, A2 a2)
    {
        typedef Creator<Servlet, T, A1, A2> CreatorType;
        std::shared_ptr<CreatorType> creator(new CreatorType());
        registerServlet(uri, std::bind(&CreatorType::create2, creator, a1,
            a2));
    }

    Servlet::ptr getServlet(const URI &uri);

    void request(std::shared_ptr<ServerRequest> request);

    static bool isWildcardPath(const URI::Path &path);
    static bool wildcardPathMatch(const URI::Path &wildPath, const URI::Path &path);

private:
    Servlet::ptr getServlet(ServletPathMapPair &vhost, const URI::Path &path);
    Servlet::ptr getServletWildcard(ServletWildcardPathMap &vhost, const URI::Path &path);

    void registerServlet(const URI &uri, const ServletOrCreator &servlet);

    static Servlet::ptr getServletPtr(ServletOrCreator &);

private:
    ServletHostMap m_servlets;
    bool m_enableWildcard;
};

}}

#endif
