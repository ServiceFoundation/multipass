/*
 * Copyright (C) 2018 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <multipass/delayed_shutdown_timer.h>
#include <multipass/logging/log.h>

#include <fmt/format.h>

namespace mp = multipass;
namespace mpl = multipass::logging;

namespace
{
std::string shutdown_message(const std::chrono::minutes& time_left)
{
    if (time_left > std::chrono::milliseconds::zero())
    {
        return fmt::format("wall The system is going down for poweroff in {} minute{}", time_left.count(),
                           time_left > std::chrono::minutes(1) ? "s" : "");
    }
    else
    {
        return fmt::format("wall The system is going down for poweroff now");
    }
}
} // namespace

mp::DelayedShutdownTimer::DelayedShutdownTimer(VirtualMachine* virtual_machine, SSHSession&& session)
    : virtual_machine{virtual_machine}, ssh_session{std::move(session)}
{
}

mp::DelayedShutdownTimer::~DelayedShutdownTimer()
{
    if (shutdown_timer.isActive())
    {
        ssh_session.exec("wall The system shutdown has been cancelled").exit_code();
        mpl::log(mpl::Level::info, virtual_machine->vm_name, fmt::format("Cancelling delayed shutdown"));
        virtual_machine->state = VirtualMachine::State::running;
    }
}

void mp::DelayedShutdownTimer::start(const std::chrono::milliseconds delay)
{
    if (virtual_machine->state == VirtualMachine::State::stopped ||
        virtual_machine->state == VirtualMachine::State::off)
        return;

    if (delay > decltype(delay)(0))
    {
        mpl::log(mpl::Level::info, virtual_machine->vm_name,
                 fmt::format("Shutdown request delayed for {} minute{}",
                             std::chrono::duration_cast<std::chrono::minutes>(delay).count(),
                             delay > std::chrono::minutes(1) ? "s" : ""));
        ssh_session.exec(shutdown_message(std::chrono::duration_cast<std::chrono::minutes>(delay)));

        time_remaining = delay;
        std::chrono::minutes time_elapsed{1};
        QObject::connect(&shutdown_timer, &QTimer::timeout, [this, delay, time_elapsed]() mutable {
            time_remaining = delay - time_elapsed;
            ssh_session.exec(shutdown_message(std::chrono::duration_cast<std::chrono::minutes>(time_remaining)));
            if (time_elapsed >= delay)
            {
                shutdown_timer.stop();
                shutdown_instance();
            }
            else
            {
                time_elapsed += std::chrono::minutes(1);
            }
        });

        virtual_machine->state = VirtualMachine::State::delayed_shutdown;

        shutdown_timer.start(std::chrono::minutes(1));
    }
    else
    {
        ssh_session.exec(shutdown_message(std::chrono::minutes::zero()));
        shutdown_instance();
    }
}

std::chrono::seconds mp::DelayedShutdownTimer::get_time_remaining()
{
    return std::chrono::duration_cast<std::chrono::minutes>(time_remaining);
}

void mp::DelayedShutdownTimer::shutdown_instance()
{
    virtual_machine->shutdown();
    emit finished();
}
