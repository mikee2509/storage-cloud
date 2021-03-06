package com.github.mikee2509.storagecloud.admin.domain.dto;

import com.github.mikee2509.storagecloud.proto.UserDetails;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Getter;

@Builder
@Getter
@EqualsAndHashCode
public class UserDto {
    private String username;
    private String firstName;
    private String lastName;
    private long capacity;
    private long usedSpace;

    public static UserDto from(UserDetails user) {
        return UserDto.builder()
                .username(user.getUsername())
                .firstName(user.getFirstName())
                .lastName(user.getLastName())
                .capacity(user.getTotalSpace())
                .usedSpace(user.getUsedSpace())
                .build();
    }
}
