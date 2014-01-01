#ifndef __MORDOR_HTTP_NEGOTIATE_AUTH_H__
#define __MORDOR_HTTP_NEGOTIATE_AUTH_H__
// Copyright (c) 2009 - Mozy, Inc.

#include <security.h>

#include <string>


namespace Mordor {

struct URI;

namespace HTTP {

struct AuthParams;
struct Request;
struct Response;

class NegotiateAuth
{
public:
    NegotiateAuth(const std::string &username, const std::string &password);
    NegotiateAuth(const NegotiateAuth& rhs) = delete;
    ~NegotiateAuth();

    bool authorize(const AuthParams &challenge, AuthParams &authorization,
        const URI &uri);

private:
    std::wstring m_username, m_password, m_domain;
    CredHandle m_creds;
    SecHandle m_secCtx;
};

}}

#endif
